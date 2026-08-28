#pragma once
#include <Arduino.h>

class EventBus;

// =====================================================
// BATTERY MONITOR
//
// Reads the 10k/10k divider on GPIO35 (ADC1, input-only,
// and ADC1 specifically because ADC2 stops working once
// Wi-Fi is on).
//
// REPORTS VOLTAGE ONLY.
//
// There is no fuel gauge on this bot. A LiPo's voltage
// curve is almost flat from roughly 3.9 V down to 3.6 V,
// which is most of its usable charge, so any percentage
// derived from voltage alone would be a guess presented
// as a fact. millivolts() is a measurement; there is
// deliberately no percent() to call.
//
// STATUS: IMPLEMENTED, hardware UNVERIFIED. Until the
// divider is actually soldered, GPIO35 floats and reads
// noise - which is why an implausible reading reports
// NotTested rather than a number.
// =====================================================

class BatteryMonitor {
 public:
  void begin();

  void update(uint32_t nowMs, EventBus& bus);

  // Battery volts in millivolts, or 0 when no plausible
  // reading has been taken. This is the measurement.
  uint16_t millivolts() const { return milliVolts_; }

  // Rough charge estimate, 0-100, from a piecewise LiPo
  // discharge curve.
  //
  // Treat this as indicative, not accurate. A cell sits
  // between about 3.7 V and 3.9 V for well over half its
  // usable charge, so the middle of this range is the
  // part a voltage reading can say least about. Under
  // load it also reads low. It is a bar on a screen, not
  // a fuel gauge.
  //
  // Returns 0 when no plausible reading exists.
  uint8_t percentEstimate() const;

  // True once readings look like a real cell rather than
  // a floating pin.
  bool hasPlausibleReading() const { return plausible_; }

 private:
  uint16_t milliVolts_ = 0;
  bool plausible_ = false;
  bool lowWarned_ = false;
  uint32_t lastSampleMs_ = 0;
};
