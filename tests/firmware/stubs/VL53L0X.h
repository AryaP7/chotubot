// Host stub of Pololu's VL53L0X driver.
//
// Returns whatever distance the test sets. The real
// ProximitySensor logic -- hysteresis, sample confirmation,
// gesture detection -- runs unmodified on top of it.

#pragma once

#include <cstdint>

namespace hostsim {
extern bool g_tofPresent;
extern uint16_t g_tofDistance;
extern bool g_tofTimeout;

inline void setTof(uint16_t mm) { g_tofDistance = mm; }
inline void setTofPresent(bool present) { g_tofPresent = present; }
inline void setTofTimeout(bool timeout) { g_tofTimeout = timeout; }

inline void resetTof() {
  g_tofPresent = true;
  g_tofDistance = 8190;  // the real sensor's "nothing there"
  g_tofTimeout = false;
}
}  // namespace hostsim

class VL53L0X {
 public:
  void setTimeout(uint16_t) {}
  bool init() { return hostsim::g_tofPresent; }
  void startContinuous(uint32_t) {}

  uint16_t readRangeContinuousMillimeters() { return hostsim::g_tofDistance; }
  bool timeoutOccurred() { return hostsim::g_tofTimeout; }
};
