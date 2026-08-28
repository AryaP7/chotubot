// Minimal host stub of the Arduino core.
//
// Only what the firmware's pure logic actually touches --
// this is not an ESP32 emulator and must never grow into
// one. Anything hardware-shaped is either a no-op or a
// value the test controls.
//
// Time is FAKE and only moves when a test moves it. That
// is what makes the touch, VAD and animation timing tests
// deterministic instead of flaky.

#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

// ---------------------------------------------------------
// Fake clock
// ---------------------------------------------------------

namespace hostsim {

extern uint32_t g_millis;

inline void setMillis(uint32_t value) { g_millis = value; }
inline void advanceMillis(uint32_t delta) { g_millis += delta; }
inline void resetClock() { g_millis = 0; }

}  // namespace hostsim

inline uint32_t millis() { return hostsim::g_millis; }
inline uint32_t micros() { return hostsim::g_millis * 1000UL; }

// delay() advances the fake clock rather than sleeping, so
// tests stay fast and deterministic.
inline void delay(uint32_t ms) { hostsim::advanceMillis(ms); }
inline void delayMicroseconds(uint32_t) {}

// ---------------------------------------------------------
// Digital IO
// ---------------------------------------------------------

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

namespace hostsim {

// 64 pins is more than the ESP32 exposes; index is the GPIO.
extern int g_pinState[64];
extern int g_pinMode[64];

inline void setPin(uint8_t pin, int value) {
  if (pin < 64) g_pinState[pin] = value;
}
inline int getPin(uint8_t pin) { return (pin < 64) ? g_pinState[pin] : LOW; }

inline void resetPins() {
  for (int i = 0; i < 64; ++i) {
    g_pinState[i] = LOW;
    g_pinMode[i] = 0;
  }
}

}  // namespace hostsim

inline void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 64) hostsim::g_pinMode[pin] = mode;
}
inline int digitalRead(uint8_t pin) { return hostsim::getPin(pin); }
inline void digitalWrite(uint8_t pin, int value) { hostsim::setPin(pin, value); }

namespace hostsim {
extern uint32_t g_adcMilliVolts;

// Set what the ADC pin reads, in millivolts AT THE PIN.
// The battery divider halves the cell, so a 3.8 V cell is
// setAdcMilliVolts(1900).
inline void setAdcMilliVolts(uint32_t mv) { g_adcMilliVolts = mv; }
inline void setBatteryCellMilliVolts(uint32_t mv) { g_adcMilliVolts = mv / 2; }
}  // namespace hostsim

inline int analogRead(uint8_t) { return 0; }
inline uint32_t analogReadMilliVolts(uint8_t pin) {
  (void)pin;
  return hostsim::g_adcMilliVolts;
}
inline void analogSetPinAttenuation(uint8_t, int) {}
#define ADC_11db 3

// ---------------------------------------------------------
// PROGMEM
//
// On the host, flash is just memory. These are plain reads.
// ---------------------------------------------------------

#define PROGMEM
#define F(x) (x)

inline uint8_t pgm_read_byte(const void* p) {
  return *reinterpret_cast<const uint8_t*>(p);
}
inline uint16_t pgm_read_word(const void* p) {
  return *reinterpret_cast<const uint16_t*>(p);
}
inline uint32_t pgm_read_dword(const void* p) {
  return *reinterpret_cast<const uint32_t*>(p);
}

// ---------------------------------------------------------
// Serial
//
// Captured rather than printed, so tests can assert on log
// output and so a noisy module does not swamp the report.
// ---------------------------------------------------------

namespace hostsim {
extern std::string g_serialLog;
inline void clearSerial() { g_serialLog.clear(); }
inline const std::string& serialLog() { return g_serialLog; }
}  // namespace hostsim

class HostSerial {
 public:
  void begin(unsigned long) {}
  int available() { return 0; }
  int read() { return -1; }

  void print(const char* s) { hostsim::g_serialLog += s; }
  void print(char c) { hostsim::g_serialLog += c; }
  void print(int v) { hostsim::g_serialLog += std::to_string(v); }
  void print(unsigned int v) { hostsim::g_serialLog += std::to_string(v); }
  void print(long v) { hostsim::g_serialLog += std::to_string(v); }
  void print(unsigned long v) { hostsim::g_serialLog += std::to_string(v); }

  void println() { hostsim::g_serialLog += "\n"; }
  void println(const char* s) {
    hostsim::g_serialLog += s;
    hostsim::g_serialLog += "\n";
  }
  void println(char c) {
    hostsim::g_serialLog += c;
    hostsim::g_serialLog += "\n";
  }
  void println(int v) { println(std::to_string(v).c_str()); }
  void println(unsigned int v) { println(std::to_string(v).c_str()); }
  void println(long v) { println(std::to_string(v).c_str()); }
  void println(unsigned long v) { println(std::to_string(v).c_str()); }

  void printf(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    hostsim::g_serialLog += buffer;
  }
};

extern HostSerial Serial;

// ---------------------------------------------------------
// Odds and ends the firmware uses
// ---------------------------------------------------------

// Returned BY VALUE deliberately. `decltype(a < b ? a : b)`
// deduces a reference when both arguments share a type,
// which would dangle on these by-value parameters. The real
// Arduino min/max are macros and have no such problem -
// this is a hazard of the stub, not of the firmware.
#ifndef min
template <typename T, typename U>
inline std::common_type_t<T, U> min(T a, U b) {
  return (a < b) ? static_cast<std::common_type_t<T, U>>(a)
                 : static_cast<std::common_type_t<T, U>>(b);
}
#endif

#ifndef max
template <typename T, typename U>
inline std::common_type_t<T, U> max(T a, U b) {
  return (a > b) ? static_cast<std::common_type_t<T, U>>(a)
                 : static_cast<std::common_type_t<T, U>>(b);
}
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

struct HostEsp {
  uint32_t getFreeHeap() const { return 200000; }
  uint32_t getSketchSize() const { return 1230785; }
  uint32_t getFreeSketchSpace() const { return 79935; }
};
extern HostEsp ESP;
