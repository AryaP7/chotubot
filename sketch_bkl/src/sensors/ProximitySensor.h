#pragma once
#include <Arduino.h>

class EventBus;

// =====================================================
// PROXIMITY - VL53L0X
//
// Turns a noisy distance stream into four stable states
// and publishes transitions to the event bus.
//
//   NoPerson -> Approaching -> Nearby
//   Nearby   -> Left        -> NoPerson
//
// Two things keep it from chattering:
//   - separate enter/exit thresholds (hysteresis)
//   - N consecutive agreeing samples before a change
//
// STATUS: IMPLEMENTED, hardware UNVERIFIED. begin()
// returns false when no sensor answers, and every later
// call becomes a no-op rather than blocking or faking a
// reading.
// =====================================================

enum class PresenceState : uint8_t {
  NoPerson = 0,
  Approaching,
  Nearby,
  Left,      // transient, collapses to NoPerson next tick
};

const char* presenceName(PresenceState state);

class ProximitySensor {
 public:
  // Call after Wire.begin(). Returns false if the sensor
  // does not respond.
  bool begin();

  // Non-blocking. Samples on its own schedule and
  // publishes events when the state changes.
  void update(uint32_t nowMs, EventBus& bus);

  bool isReady() const { return ready_; }
  PresenceState state() const { return state_; }

  // Last accepted distance in mm. 0 when out of range or
  // unavailable - never a fabricated value.
  uint16_t lastDistanceMm() const { return lastDistanceMm_; }

  // Distance gestures: approach, retreat, hover.
  //
  // OFF BY DEFAULT, deliberately. The thresholds below
  // are reasoned guesses, not measurements - nobody has
  // waved a hand at this sensor yet. Turn them on with
  // serial 'g' to tune them, and only wire them into
  // behaviour once they prove reliable.
  void setGesturesEnabled(bool enabled) { gesturesEnabled_ = enabled; }
  bool gesturesEnabled() const { return gesturesEnabled_; }

 private:
  void detectGestures(uint16_t mm, uint32_t nowMs, EventBus& bus);

  PresenceState classify(uint16_t mm) const;

  bool ready_ = false;
  PresenceState state_ = PresenceState::NoPerson;

  PresenceState candidate_ = PresenceState::NoPerson;
  uint8_t candidateCount_ = 0;

  uint16_t lastDistanceMm_ = 0;
  uint32_t lastSampleMs_ = 0;

  bool gesturesEnabled_ = false;
  uint16_t gestureRef_ = 0;
  uint32_t gestureRefMs_ = 0;
  uint32_t gestureCooldownMs_ = 0;
  uint32_t hoverSinceMs_ = 0;
  bool hoverFired_ = false;
};
