#include "BatteryMonitor.h"

#include "../Config.h"
#include "../core/Events.h"

namespace {

constexpr uint32_t kSampleIntervalMs = 5000;
constexpr uint8_t kOversample = 16;

// A single cell that is connected and not destroyed sits
// somewhere in this band. Anything outside it is a
// floating pin, not a battery.
constexpr uint16_t kPlausibleMinMv = 2500;
constexpr uint16_t kPlausibleMaxMv = 4400;

// Protected TP4056 boards cut off near 2.9 V. Warn well
// before that so the bot can say something first.
constexpr uint16_t kLowMv = 3400;

}  // namespace

namespace {

// Piecewise open-circuit discharge curve for a single
// LiPo cell. Linear interpolation between these points is
// far closer to reality than mapping 3.0-4.2 V straight
// onto 0-100%, which would read ~50% when the cell is
// nearly flat.
struct CurvePoint {
  uint16_t mv;
  uint8_t pct;
};

constexpr CurvePoint kCurve[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3950, 70}, {3870, 60}, {3820, 50},
    {3790, 40},  {3770, 30}, {3740, 20}, {3680, 10}, {3450, 5},  {3000, 0},
};

constexpr uint8_t kCurveLen = sizeof(kCurve) / sizeof(kCurve[0]);

}  // namespace

uint8_t BatteryMonitor::percentEstimate() const {
  if (!plausible_ || milliVolts_ == 0) return 0;

  if (milliVolts_ >= kCurve[0].mv) return 100;
  if (milliVolts_ <= kCurve[kCurveLen - 1].mv) return 0;

  for (uint8_t i = 1; i < kCurveLen; ++i) {
    if (milliVolts_ >= kCurve[i].mv) {
      const CurvePoint& hi = kCurve[i - 1];
      const CurvePoint& lo = kCurve[i];

      const uint16_t span = hi.mv - lo.mv;
      if (span == 0) return lo.pct;

      const uint32_t into = milliVolts_ - lo.mv;
      const uint32_t range = hi.pct - lo.pct;
      return static_cast<uint8_t>(lo.pct + (into * range) / span);
    }
  }

  return 0;
}

void BatteryMonitor::begin() {
  // 11 dB attenuation gives roughly 0 - 2.5 V of usable
  // range, which suits the halved cell voltage.
  analogSetPinAttenuation(Config::PIN_BATT_SENSE, ADC_11db);
}

void BatteryMonitor::update(uint32_t nowMs, EventBus& bus) {
  if (nowMs - lastSampleMs_ < kSampleIntervalMs) return;
  lastSampleMs_ = nowMs;

  // analogReadMilliVolts applies the chip's factory ADC
  // calibration, which is far better than scaling the raw
  // count by hand.
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kOversample; ++i) {
    sum += analogReadMilliVolts(Config::PIN_BATT_SENSE);
  }

  const uint32_t atPin = sum / kOversample;

  // The divider halves it, so the cell is twice the pin.
  const uint32_t cell = atPin * 2;

  plausible_ = (cell >= kPlausibleMinMv && cell <= kPlausibleMaxMv);
  milliVolts_ = plausible_ ? static_cast<uint16_t>(cell) : 0;

  if (!plausible_) {
    lowWarned_ = false;
    return;
  }

  // Warn once per crossing, not once per sample.
  if (milliVolts_ < kLowMv) {
    if (!lowWarned_) {
      lowWarned_ = true;
      bus.publish(EventType::BatteryLow, milliVolts_);
    }
  } else if (milliVolts_ > kLowMv + 100) {
    lowWarned_ = false;
  }
}
