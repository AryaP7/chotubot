#pragma once
#include <Arduino.h>

class EventBus;
class RtcManager;

// =====================================================
// SCHEDULER - alarms, quiet hours, time-aware states
//
// Runs off the DS3231, so everything here keeps working
// with no Wi-Fi and no backend.
//
// Alarms persist in NVS (Preferences), not in RAM, so
// they survive a reflash. The RTC itself is never
// written on boot - see RtcManager.
// =====================================================

constexpr uint8_t kMaxAlarms = 6;

// Bit 0 = Sunday .. bit 6 = Saturday. 0x7F is every day.
constexpr uint8_t kEveryDay = 0x7F;

struct Alarm {
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t daysMask = 0;    // 0 = disabled
  bool enabled = false;
};

enum class TimeOfDay : uint8_t {
  Night = 0,   // 23:00 - 05:59
  Morning,     // 06:00 - 11:59
  Afternoon,   // 12:00 - 17:59
  Evening,     // 18:00 - 22:59
};

const char* timeOfDayName(TimeOfDay tod);

class Scheduler {
 public:
  void begin();

  // Non-blocking. Checks at most once per minute-change.
  void update(uint32_t nowMs, RtcManager& rtc, EventBus& bus);

  bool setAlarm(uint8_t index, uint8_t hour, uint8_t minute,
                uint8_t daysMask);
  bool clearAlarm(uint8_t index);
  const Alarm& alarm(uint8_t index) const;

  // Quiet hours: the bot dims, stops announcing and
  // prefers the sleepy expression.
  void setQuietHours(uint8_t startHour, uint8_t endHour);
  bool isQuiet() const { return quietNow_; }

  TimeOfDay timeOfDay() const { return timeOfDay_; }

  void printSerial() const;

 private:
  void load();
  void save() const;
  bool matchesNow(const Alarm& a, uint8_t hour, uint8_t minute,
                  uint8_t dayOfWeek) const;

  Alarm alarms_[kMaxAlarms];

  uint8_t quietStartHour_ = 23;
  uint8_t quietEndHour_ = 7;
  bool quietNow_ = false;

  TimeOfDay timeOfDay_ = TimeOfDay::Morning;

  // Guards against firing an alarm repeatedly inside the
  // same minute.
  int16_t lastCheckedMinute_ = -1;
  uint32_t lastPollMs_ = 0;
};
