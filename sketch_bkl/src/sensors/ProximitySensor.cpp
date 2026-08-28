#include "ProximitySensor.h"

#include <VL53L0X.h>

#include "../Config.h"
#include "../core/Events.h"

// Pololu's driver rather than Adafruit's: it has no
// dependencies, no dynamic allocation, and a much
// smaller footprint. We only need single distances, not
// the full API surface.
static VL53L0X sensor;

const char* presenceName(PresenceState state) {
  switch (state) {
    case PresenceState::NoPerson:    return "NO_PERSON";
    case PresenceState::Approaching: return "APPROACHING";
    case PresenceState::Nearby:      return "NEARBY";
    case PresenceState::Left:        return "LEFT";
  }
  return "?";
}

bool ProximitySensor::begin() {
  sensor.setTimeout(Config::TOF_IO_TIMEOUT_MS);

  if (!sensor.init()) {
    Serial.println(F("[tof] VL53L0X not found"));
    ready_ = false;
    return false;
  }

  // Continuous mode at our sample interval. Reads then
  // return an already-captured value instead of waiting
  // for a fresh conversion.
  sensor.startContinuous(Config::TOF_SAMPLE_INTERVAL_MS);

  ready_ = true;
  Serial.println(F("[tof] VL53L0X ready"));
  return true;
}

PresenceState ProximitySensor::classify(uint16_t mm) const {
  const bool nothingThere = (mm == 0) || (mm > Config::TOF_MAX_VALID_MM);
  if (nothingThere) return PresenceState::NoPerson;

  // Asymmetric thresholds: how far you must come in to
  // enter a state differs from how far you must back off
  // to leave it.
  switch (state_) {
    case PresenceState::Nearby:
      if (mm > Config::TOF_APPROACH_EXIT_MM) return PresenceState::NoPerson;
      if (mm > Config::TOF_NEARBY_EXIT_MM) return PresenceState::Approaching;
      return PresenceState::Nearby;

    case PresenceState::Approaching:
      if (mm <= Config::TOF_NEARBY_ENTER_MM) return PresenceState::Nearby;
      if (mm > Config::TOF_APPROACH_EXIT_MM) return PresenceState::NoPerson;
      return PresenceState::Approaching;

    case PresenceState::NoPerson:
    case PresenceState::Left:
    default:
      if (mm <= Config::TOF_NEARBY_ENTER_MM) return PresenceState::Nearby;
      if (mm <= Config::TOF_APPROACH_ENTER_MM) return PresenceState::Approaching;
      return PresenceState::NoPerson;
  }
}

void ProximitySensor::detectGestures(uint16_t mm, uint32_t nowMs,
                                     EventBus& bus) {
  // Tunables. Named here rather than in Config because
  // they are unproven and expected to change once there
  // is real data.
  constexpr uint16_t kMoveThresholdMm = 250;
  constexpr uint32_t kMoveWindowMs = 450;
  constexpr uint16_t kHoverJitterMm = 45;
  constexpr uint32_t kHoverHoldMs = 1000;
  constexpr uint32_t kCooldownMs = 700;

  const bool inRange = (mm > 0 && mm <= Config::TOF_MAX_VALID_MM);
  if (!inRange) {
    gestureRef_ = 0;
    hoverSinceMs_ = 0;
    hoverFired_ = false;
    return;
  }

  if (gestureRef_ == 0) {
    gestureRef_ = mm;
    gestureRefMs_ = nowMs;
    hoverSinceMs_ = nowMs;
    return;
  }

  const int32_t delta = static_cast<int32_t>(mm) - gestureRef_;

  // ---- Hover: held still, close in ----
  if (abs(delta) <= kHoverJitterMm) {
    if (hoverSinceMs_ == 0) hoverSinceMs_ = nowMs;

    if (!hoverFired_ && mm <= Config::TOF_NEARBY_ENTER_MM &&
        (nowMs - hoverSinceMs_) >= kHoverHoldMs) {
      hoverFired_ = true;
      bus.publish(EventType::GestureHover, mm);
    }
  } else {
    hoverSinceMs_ = nowMs;
    hoverFired_ = false;
  }

  // ---- Approach / retreat: a decisive move ----
  if (nowMs < gestureCooldownMs_) {
    // Still let the reference age out during cooldown.
    if (nowMs - gestureRefMs_ > kMoveWindowMs) {
      gestureRef_ = mm;
      gestureRefMs_ = nowMs;
    }
    return;
  }

  if ((nowMs - gestureRefMs_) <= kMoveWindowMs) {
    if (delta <= -static_cast<int32_t>(kMoveThresholdMm)) {
      bus.publish(EventType::GestureApproach, mm);
      gestureCooldownMs_ = nowMs + kCooldownMs;
      gestureRef_ = mm;
      gestureRefMs_ = nowMs;
    } else if (delta >= static_cast<int32_t>(kMoveThresholdMm)) {
      bus.publish(EventType::GestureRetreat, mm);
      gestureCooldownMs_ = nowMs + kCooldownMs;
      gestureRef_ = mm;
      gestureRefMs_ = nowMs;
    }
  } else {
    // Window closed without a decisive move - re-anchor.
    gestureRef_ = mm;
    gestureRefMs_ = nowMs;
  }
}

void ProximitySensor::update(uint32_t nowMs, EventBus& bus) {
  if (!ready_) return;

  // Left is transient: it exists so callers can observe
  // the departure, then collapses without a second event.
  if (state_ == PresenceState::Left) {
    state_ = PresenceState::NoPerson;
    candidate_ = PresenceState::NoPerson;
    candidateCount_ = 0;
  }

  if (nowMs - lastSampleMs_ < Config::TOF_SAMPLE_INTERVAL_MS) return;
  lastSampleMs_ = nowMs;

  const uint16_t mm = sensor.readRangeContinuousMillimeters();

  if (sensor.timeoutOccurred()) {
    // Report it once per occurrence and carry on. A
    // wedged sensor must not take the bot down.
    bus.publish(EventType::SensorError, 0);
    return;
  }

  if (gesturesEnabled_) {
    detectGestures(mm, nowMs, bus);
  }

  const PresenceState observed = classify(mm);

  // Require several agreeing samples before committing.
  if (observed == candidate_) {
    if (candidateCount_ < 255) candidateCount_++;
  } else {
    candidate_ = observed;
    candidateCount_ = 1;
  }

  if (candidateCount_ < Config::TOF_CONFIRM_SAMPLES) return;
  if (observed == state_) return;

  const PresenceState previous = state_;
  state_ = observed;
  lastDistanceMm_ = (observed == PresenceState::NoPerson) ? 0 : mm;

  switch (observed) {
    case PresenceState::Nearby:
      bus.publish(EventType::PersonDetected, mm);
      break;

    case PresenceState::Approaching:
      // Only announce an approach on the way in. Backing
      // off from Nearby is a departure, not an arrival.
      if (previous == PresenceState::NoPerson) {
        bus.publish(EventType::PersonApproaching, mm);
      }
      break;

    case PresenceState::NoPerson:
      if (previous != PresenceState::NoPerson) {
        // Pass through Left so the departure is
        // observable, then collapse on the next tick.
        state_ = PresenceState::Left;
        bus.publish(EventType::PersonLeft, 0);
      }
      break;

    case PresenceState::Left:
      break;
  }
}
