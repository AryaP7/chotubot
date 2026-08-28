#pragma once
#include <Arduino.h>

// =====================================================
// TOUCH INPUT - TTP223B
//
// Non-blocking gesture recogniser: short, double, triple
// and long press.
//
// TIMING NOTE - this changes V1 behaviour
// A single tap now fires on RELEASE, not on press. It
// has to: until the finger lifts you cannot know whether
// a press is going to become a long press or the first
// of a double tap. The practical cost is that a tap
// registers a few tens of ms later than it used to.
//
// A long press is the exception - it fires while the
// finger is still down, the moment the threshold passes,
// because waiting for release would feel broken.
// =====================================================

enum class TouchEvent : uint8_t {
  None = 0,
  Short,
  Double,
  Long,
  Triple,
};

const char* touchEventName(TouchEvent event);

class TouchInput {
 public:
  void begin();

  // Call every loop. Returns at most one event per tick.
  TouchEvent update(uint32_t nowMs);

  bool isHeld() const { return pressed_; }

 private:
  bool pressed_ = false;

  // Debounced edge tracking.
  bool rawLast_ = false;
  uint32_t lastEdgeMs_ = 0;

  uint32_t pressStartedMs_ = 0;
  bool longFired_ = false;

  // Taps counted inside the current multi-tap window.
  uint8_t tapCount_ = 0;
  uint32_t lastReleaseMs_ = 0;
};
