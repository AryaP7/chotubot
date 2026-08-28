#pragma once
#include <Arduino.h>
#include <RTClib.h>

// =====================================================
// RTC MANAGER - DS3231
//
// Wraps RTClib so the rest of the project never touches
// it directly. A missing DS3231 is reported, not fatal:
// the old code spun forever in a while(true), which took
// the animation down with it.
// =====================================================

class RtcManager {
 public:
  bool begin();

  bool isReady() const { return ready_; }

  // Valid only when isReady(). Returns a zeroed DateTime
  // otherwise so callers cannot read garbage.
  DateTime now();

  // True when the chip reports it lost power and the
  // time is therefore not trustworthy.
  bool lostPower() const { return lostPower_; }

  // Deliberately NOT called on boot. Setting the clock is
  // an explicit action, so a reflash does not silently
  // rewrite the time.
  void setDateTime(const DateTime& dt);

 private:
  RTC_DS3231 rtc_;
  bool ready_ = false;
  bool lostPower_ = false;
};
