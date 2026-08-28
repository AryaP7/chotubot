#pragma once
#include <Arduino.h>

class DisplayManager;
struct Notification;

// =====================================================
// NOTIFICATION SCREEN
//
// Shows a normalized event from the backend, then gets
// out of the way. Word-wraps the body across two lines
// and dismisses itself after a few seconds so the bot
// returns to whatever it was doing.
// =====================================================

class NotificationScreen {
 public:
  void show(uint32_t nowMs);

  bool update(DisplayManager& display, const Notification& notification,
              uint32_t nowMs);

  // True once the display timeout has elapsed.
  bool isExpired(uint32_t nowMs) const;

 private:
  uint32_t shownAtMs_ = 0;
  uint32_t lastRenderMs_ = 0;
};
