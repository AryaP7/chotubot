#pragma once
#include <Arduino.h>

class DisplayManager;
struct SpotifyState;

// =====================================================
// NOW PLAYING - Spotify on a 128x64 panel
//
//   +----------------+
//   |  NOW PLAYING   |
//   | Blinding       |
//   | Lights         |
//   | The Weeknd     |
//   |  1:24 ---- 3:12|
//   +----------------+
//
// Track and artist scroll horizontally when they do not
// fit rather than being cut off. The ESP32 is a display
// and a controller here - audio plays on a real Spotify
// device driven by the backend.
// =====================================================

class NowPlayingScreen {
 public:
  void invalidate() { lastRenderMs_ = 0; scrollOffset_ = 0; }

  // Non-blocking, self-throttled.
  bool update(DisplayManager& display, const SpotifyState& state,
              uint32_t nowMs);

 private:
  uint32_t lastRenderMs_ = 0;
  uint32_t lastScrollMs_ = 0;
  uint16_t scrollOffset_ = 0;
};
