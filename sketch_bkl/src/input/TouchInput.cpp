#include "TouchInput.h"
#include "../Config.h"

const char* touchEventName(TouchEvent event) {
  switch (event) {
    case TouchEvent::Short:  return "SHORT";
    case TouchEvent::Double: return "DOUBLE";
    case TouchEvent::Long:   return "LONG";
    case TouchEvent::Triple: return "TRIPLE";
    case TouchEvent::None:   return "NONE";
  }
  return "?";
}

void TouchInput::begin() {
  pinMode(Config::PIN_TOUCH, INPUT);
  rawLast_ = digitalRead(Config::PIN_TOUCH);
  pressed_ = rawLast_;
}

TouchEvent TouchInput::update(uint32_t nowMs) {
  const bool raw = digitalRead(Config::PIN_TOUCH);

  // Edge debounce: ignore changes that arrive faster than
  // the contact can physically settle.
  if (raw != rawLast_) {
    rawLast_ = raw;
    lastEdgeMs_ = nowMs;
  }

  const bool settled = (nowMs - lastEdgeMs_) >= Config::TOUCH_DEBOUNCE_MS;

  if (settled && raw != pressed_) {
    pressed_ = raw;

    if (pressed_) {
      pressStartedMs_ = nowMs;
      longFired_ = false;
    } else {
      // Released. A press that already fired as a long
      // press must not also count as a tap.
      if (!longFired_) {
        tapCount_++;
        lastReleaseMs_ = nowMs;
      }
    }
  }

  // Long press fires while still held.
  if (pressed_ && !longFired_ &&
      (nowMs - pressStartedMs_) >= Config::TOUCH_LONG_MS) {
    longFired_ = true;
    tapCount_ = 0;
    return TouchEvent::Long;
  }

  // Triple resolves immediately - nothing can extend it.
  if (tapCount_ >= 3) {
    tapCount_ = 0;
    return TouchEvent::Triple;
  }

  // One or two taps only resolve once the window for
  // another tap has closed.
  if (tapCount_ > 0 && !pressed_ &&
      (nowMs - lastReleaseMs_) >= Config::TOUCH_MULTI_WINDOW_MS) {
    const uint8_t count = tapCount_;
    tapCount_ = 0;
    return (count == 1) ? TouchEvent::Short : TouchEvent::Double;
  }

  return TouchEvent::None;
}
