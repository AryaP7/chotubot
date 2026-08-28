#include "Scheduler.h"

#include <Preferences.h>

#include "../core/Events.h"
#include "RtcManager.h"

namespace {

// Poll interval. The RTC only changes once a minute, so
// there is no reason to hit the I2C bus faster than this.
constexpr uint32_t kPollIntervalMs = 1000;

constexpr const char* kNamespace = "chotubot";
constexpr const char* kAlarmsKey = "alarms";
constexpr const char* kQuietKey = "quiet";

TimeOfDay classifyHour(uint8_t hour) {
  if (hour >= 6 && hour < 12) return TimeOfDay::Morning;
  if (hour >= 12 && hour < 18) return TimeOfDay::Afternoon;
  if (hour >= 18 && hour < 23) return TimeOfDay::Evening;
  return TimeOfDay::Night;
}

}  // namespace

const char* timeOfDayName(TimeOfDay tod) {
  switch (tod) {
    case TimeOfDay::Night:     return "NIGHT";
    case TimeOfDay::Morning:   return "MORNING";
    case TimeOfDay::Afternoon: return "AFTERNOON";
    case TimeOfDay::Evening:   return "EVENING";
  }
  return "?";
}

void Scheduler::begin() {
  load();
}

void Scheduler::load() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;

  prefs.getBytes(kAlarmsKey, alarms_, sizeof(alarms_));

  const uint16_t quiet = prefs.getUShort(kQuietKey, (23 << 8) | 7);
  quietStartHour_ = static_cast<uint8_t>(quiet >> 8);
  quietEndHour_ = static_cast<uint8_t>(quiet & 0xFF);

  prefs.end();
}

void Scheduler::save() const {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;

  prefs.putBytes(kAlarmsKey, alarms_, sizeof(alarms_));
  prefs.putUShort(kQuietKey,
                  static_cast<uint16_t>((quietStartHour_ << 8) | quietEndHour_));
  prefs.end();
}

bool Scheduler::setAlarm(uint8_t index, uint8_t hour, uint8_t minute,
                         uint8_t daysMask) {
  if (index >= kMaxAlarms || hour > 23 || minute > 59) return false;

  alarms_[index].hour = hour;
  alarms_[index].minute = minute;
  alarms_[index].daysMask = daysMask;
  alarms_[index].enabled = (daysMask != 0);
  save();
  return true;
}

bool Scheduler::clearAlarm(uint8_t index) {
  if (index >= kMaxAlarms) return false;
  alarms_[index] = Alarm{};
  save();
  return true;
}

const Alarm& Scheduler::alarm(uint8_t index) const {
  static const Alarm empty;
  return (index < kMaxAlarms) ? alarms_[index] : empty;
}

void Scheduler::setQuietHours(uint8_t startHour, uint8_t endHour) {
  if (startHour > 23 || endHour > 23) return;
  quietStartHour_ = startHour;
  quietEndHour_ = endHour;
  save();
}

bool Scheduler::matchesNow(const Alarm& a, uint8_t hour, uint8_t minute,
                           uint8_t dayOfWeek) const {
  if (!a.enabled || a.daysMask == 0) return false;
  if (a.hour != hour || a.minute != minute) return false;
  return (a.daysMask & (1 << dayOfWeek)) != 0;
}

void Scheduler::update(uint32_t nowMs, RtcManager& rtc, EventBus& bus) {
  if (!rtc.isReady()) return;

  if (nowMs - lastPollMs_ < kPollIntervalMs) return;
  lastPollMs_ = nowMs;

  const DateTime now = rtc.now();
  const int16_t minuteOfDay =
      static_cast<int16_t>(now.hour()) * 60 + now.minute();

  if (minuteOfDay == lastCheckedMinute_) return;
  lastCheckedMinute_ = minuteOfDay;

  // ---- Alarms ----
  for (uint8_t i = 0; i < kMaxAlarms; ++i) {
    if (matchesNow(alarms_[i], now.hour(), now.minute(), now.dayOfTheWeek())) {
      bus.publish(EventType::AlarmFired, i);
    }
  }

  // ---- Quiet hours ----
  // The window may wrap past midnight, so the comparison
  // differs depending on which way round it is.
  const uint8_t h = now.hour();
  const bool quiet = (quietStartHour_ <= quietEndHour_)
                         ? (h >= quietStartHour_ && h < quietEndHour_)
                         : (h >= quietStartHour_ || h < quietEndHour_);

  if (quiet != quietNow_) {
    quietNow_ = quiet;
    bus.publish(quiet ? EventType::QuietHoursStarted
                      : EventType::QuietHoursEnded);
  }

  // ---- Time of day ----
  const TimeOfDay tod = classifyHour(h);
  if (tod != timeOfDay_) {
    timeOfDay_ = tod;
    bus.publish(EventType::TimeOfDayChanged, static_cast<int32_t>(tod));
  }
}

void Scheduler::printSerial() const {
  Serial.println(F("--- SCHEDULE ---"));
  for (uint8_t i = 0; i < kMaxAlarms; ++i) {
    const Alarm& a = alarms_[i];
    Serial.print(i);
    Serial.print('\t');
    if (!a.enabled) {
      Serial.println(F("(free)"));
      continue;
    }
    Serial.printf("%02u:%02u  days=0x%02X\n", a.hour, a.minute, a.daysMask);
  }
  Serial.printf("quiet\t%02u:00 - %02u:00  now=%s\n", quietStartHour_,
                quietEndHour_, quietNow_ ? "YES" : "no");
  Serial.print(F("period\t"));
  Serial.println(timeOfDayName(timeOfDay_));
}
