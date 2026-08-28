#include "LedController.h"

#include <Adafruit_NeoPixel.h>

#include "../Config.h"

// Adafruit's driver rather than FastLED: roughly a fifth
// of the flash, and we need addressable output, not
// FastLED's colour science and effect engine.
static Adafruit_NeoPixel strip(Config::LED_COUNT, Config::PIN_LED_DATA,
                               NEO_GRB + NEO_KHZ800);

const char* ledModeName(LedMode mode) {
  switch (mode) {
    case LedMode::Off:          return "OFF";
    case LedMode::Idle:         return "IDLE";
    case LedMode::Listening:    return "LISTENING";
    case LedMode::Thinking:     return "THINKING";
    case LedMode::Speaking:     return "SPEAKING";
    case LedMode::Notification: return "NOTIFICATION";
    case LedMode::Error:        return "ERROR";
    case LedMode::Sleep:        return "SLEEP";
    case LedMode::Music:        return "MUSIC";
  }
  return "?";
}

namespace {

// Triangle wave over a 0-255 phase. Cheaper than sin()
// and smooth enough for a breathe.
uint8_t triangle(uint8_t phase) {
  return (phase < 128) ? static_cast<uint8_t>(phase * 2)
                       : static_cast<uint8_t>((255 - phase) * 2);
}

uint8_t scale(uint8_t value, uint8_t factor) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * factor) / 255);
}

}  // namespace

void LedController::begin() {
  strip.begin();
  strip.setBrightness(Config::LED_MAX_BRIGHTNESS);
  strip.clear();
  strip.show();
  ready_ = true;
}

void LedController::setMode(LedMode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  phase_ = 0;
}

void LedController::renderIdle(uint8_t phase) {
  // Slow cyan breathe.
  const uint8_t level = scale(triangle(phase), 90);
  strip.fill(strip.Color(0, level, scale(level, 160)));
}

void LedController::renderListening(uint8_t phase) {
  // Brighter, faster blue pulse.
  const uint8_t level = triangle(static_cast<uint8_t>(phase * 3));
  strip.fill(strip.Color(0, scale(level, 120), level));
}

void LedController::renderThinking(uint8_t phase) {
  // Single amber dot chasing round the strip.
  strip.clear();
  const uint16_t head = (phase / 8) % Config::LED_COUNT;
  strip.setPixelColor(head, strip.Color(255, 140, 0));

  const uint16_t tail = (head + Config::LED_COUNT - 1) % Config::LED_COUNT;
  strip.setPixelColor(tail, strip.Color(60, 30, 0));
}

void LedController::renderSpeaking(uint8_t phase) {
  // Warm wave travelling outward from the centre.
  for (uint16_t i = 0; i < Config::LED_COUNT; ++i) {
    const uint8_t local = static_cast<uint8_t>(phase * 2 + i * 24);
    const uint8_t level = triangle(local);
    strip.setPixelColor(i, strip.Color(level, scale(level, 200), 30));
  }
}

void LedController::renderNotification(uint8_t phase) {
  // Deliberately hard on/off - this is meant to catch
  // your eye, not look pretty.
  const bool on = ((phase / 32) % 2) == 0;
  strip.fill(on ? strip.Color(255, 200, 0) : 0);
}

void LedController::renderError(uint8_t phase) {
  const uint8_t level = scale(triangle(static_cast<uint8_t>(phase * 2)), 200);
  strip.fill(strip.Color(level, 0, 0));
}

void LedController::renderSleep(uint8_t phase) {
  // Barely-there ember so you can tell it is alive.
  const uint8_t level = scale(triangle(phase), 24);
  strip.fill(strip.Color(level, 0, scale(level, 120)));
}

void LedController::renderMusic(uint8_t phase) {
  // Hue sweep across the strip. Placeholder until real
  // playback data arrives from the backend - it is not
  // reacting to audio and does not pretend to.
  for (uint16_t i = 0; i < Config::LED_COUNT; ++i) {
    const uint16_t hue =
        static_cast<uint16_t>((phase * 257) + (i * (65536 / Config::LED_COUNT)));
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue, 255, 200)));
  }
}

void LedController::update(uint32_t nowMs) {
  if (!ready_) return;

  if (nowMs - lastFrameMs_ < Config::LED_FRAME_INTERVAL_MS) return;
  lastFrameMs_ = nowMs;

  phase_++;

  switch (mode_) {
    case LedMode::Off:          strip.clear();                break;
    case LedMode::Idle:         renderIdle(phase_);           break;
    case LedMode::Listening:    renderListening(phase_);      break;
    case LedMode::Thinking:     renderThinking(phase_);       break;
    case LedMode::Speaking:     renderSpeaking(phase_);       break;
    case LedMode::Notification: renderNotification(phase_);   break;
    case LedMode::Error:        renderError(phase_);          break;
    case LedMode::Sleep:        renderSleep(phase_);          break;
    case LedMode::Music:        renderMusic(phase_);          break;
  }

  strip.show();
}
