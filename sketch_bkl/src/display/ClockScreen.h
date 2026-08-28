#pragma once
#include <Arduino.h>

class DisplayManager;
class RtcManager;

// =====================================================
// CLOCK SCREEN
//
// Same layout the hardware build already shows. The only
// change is that it redraws when the second actually
// ticks instead of on every loop behind a delay(100).
// =====================================================

class ClockScreen {
 public:
  // Force a full redraw on the next update, e.g. when
  // the screen is entered.
  void invalidate() { lastSecond_ = 0xFF; }

  // Non-blocking. Returns true if it pushed a frame.
  bool update(DisplayManager& display, RtcManager& rtc);

 private:
  uint8_t lastSecond_ = 0xFF;
};
