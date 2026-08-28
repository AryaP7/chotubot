#pragma once
#include <Arduino.h>

class EventBus;

// =====================================================
// WIFI MANAGER
//
// Non-blocking connect and reconnect with backoff.
//
// OFFLINE-FIRST: nothing in here may ever block the main
// loop. Mochi, touch, the clock, the LEDs and the ToF
// sensor all keep working with no network at all - the
// only thing that degrades is the backend.
// =====================================================

enum class WifiState : uint8_t {
  Disabled = 0,   // no credentials configured
  Idle,           // waiting to attempt
  Connecting,
  Connected,
  Failed,
};

const char* wifiStateName(WifiState state);

class WifiManager {
 public:
  void begin();

  // Non-blocking. Drives the connect state machine and
  // publishes WifiConnected / WifiDisconnected.
  void update(uint32_t nowMs, EventBus& bus);

  WifiState state() const { return state_; }
  bool isConnected() const { return state_ == WifiState::Connected; }

  // Empty until connected.
  const char* ipAddress() const { return ip_; }

 private:
  void startAttempt(uint32_t nowMs);

  WifiState state_ = WifiState::Idle;
  uint32_t attemptStartedMs_ = 0;
  uint32_t nextAttemptMs_ = 0;
  uint8_t failureCount_ = 0;
  char ip_[16] = {0};
};
