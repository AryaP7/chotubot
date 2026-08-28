#pragma once
#include <Arduino.h>

class DisplayManager;

// =====================================================
// DIAGNOSTICS
//
// Honest subsystem status. Everything starts at
// NotTested and may only be promoted to Ok by code that
// actually talked to the hardware. See DECISIONS.md D6.
// =====================================================

enum class Subsystem : uint8_t {
  Oled = 0,
  Rtc,
  Touch,
  Wifi,
  Microphone,
  Audio,
  Proximity,
  Lighting,
  Backend,
  COUNT
};

enum class Health : uint8_t {
  NotTested = 0,   // not wired, or never exercised
  Ok,
  Failed,
};

class Diagnostics {
 public:
  void set(Subsystem sub, Health health) {
    health_[static_cast<uint8_t>(sub)] = health;
  }

  Health get(Subsystem sub) const {
    return health_[static_cast<uint8_t>(sub)];
  }

  // Full-screen status page on the SH1106. Self-throttled
  // to 2 Hz - this pushes 1 KB over I2C and nothing on it
  // changes faster than that.
  void render(DisplayManager& display, uint32_t nowMs);

  void printSerial() const;

  uint32_t freeHeap() const;
  uint32_t uptimeSeconds() const { return millis() / 1000UL; }

  // Flash actually occupied by this build, and the size
  // of the partition it sits in.
  uint32_t sketchBytes() const;
  uint32_t sketchCapacityBytes() const;

  // Latest battery reading. percent is an estimate from
  // the voltage curve - see BatteryMonitor.
  void setBattery(uint16_t milliVolts, uint8_t percent, bool valid) {
    battMv_ = milliVolts;
    battPct_ = percent;
    battValid_ = valid;
  }

 private:
  Health health_[static_cast<uint8_t>(Subsystem::COUNT)] = {Health::NotTested};
  uint32_t lastRenderMs_ = 0;
  bool rendered_ = false;

  uint16_t battMv_ = 0;
  uint8_t battPct_ = 0;
  bool battValid_ = false;
};

const char* subsystemName(Subsystem sub);
const char* healthName(Health health);
