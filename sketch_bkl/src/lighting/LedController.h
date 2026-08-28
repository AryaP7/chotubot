#pragma once
#include <Arduino.h>

// =====================================================
// LIGHTING - WS2812B
//
// State-driven, non-blocking. Patterns advance from a
// phase counter on a timer; nothing here loops, sleeps
// or busy-waits.
//
// Brightness is capped in firmware (Config::
// LED_MAX_BRIGHTNESS) because the MT3608 cannot supply
// full-white LEDs and loud audio at the same time.
//
// STATUS: IMPLEMENTED, hardware UNVERIFIED. A WS2812B
// strip cannot be detected - there is no readback line -
// so this never reports itself healthy. See
// DECISIONS.md D6.
// =====================================================

enum class LedMode : uint8_t {
  Off = 0,
  Idle,
  Listening,
  Thinking,
  Speaking,
  Notification,
  Error,
  Sleep,
  Music,
};

const char* ledModeName(LedMode mode);

class LedController {
 public:
  void begin();

  void setMode(LedMode mode);
  LedMode mode() const { return mode_; }

  // Non-blocking. Call every loop.
  void update(uint32_t nowMs);

 private:
  void renderIdle(uint8_t phase);
  void renderListening(uint8_t phase);
  void renderThinking(uint8_t phase);
  void renderSpeaking(uint8_t phase);
  void renderNotification(uint8_t phase);
  void renderError(uint8_t phase);
  void renderSleep(uint8_t phase);
  void renderMusic(uint8_t phase);

  LedMode mode_ = LedMode::Off;
  uint8_t phase_ = 0;
  uint32_t lastFrameMs_ = 0;
  bool ready_ = false;
};
