#pragma once
#include <Arduino.h>

// =====================================================
// EVENT BUS
//
// Fixed-size ring buffer. No heap, no std::function, no
// dynamic registration - subsystems publish, the state
// machine drains once per loop.
//
// Cost: kQueueSize * 8 bytes of RAM. Nothing else.
// =====================================================

enum class EventType : uint8_t {
  None = 0,

  // Input - TTP223B
  TouchShort,
  TouchDouble,   // PLANNED
  TouchLong,     // PLANNED
  TouchTriple,   // PLANNED

  // Presence - VL53L0X            PLANNED (not wired)
  PersonApproaching,
  PersonDetected,
  PersonLeft,

  // Distance gestures. Disabled by default until the
  // thresholds are tuned against real sensor data.
  GestureApproach,
  GestureRetreat,
  GestureHover,

  // Time - DS3231
  AlarmFired,
  QuietHoursStarted,
  QuietHoursEnded,
  TimeOfDayChanged,

  // Power
  BatteryLow,

  // Voice - INMP441 + local VAD   PLANNED (not wired)
  VoiceStarted,
  VoiceStopped,

  // Backend round trip            PLANNED (no Wi-Fi yet)
  AiRequest,
  AiResponse,
  AiError,
  WakeWordConfirmed,   // backend said yes - see DECISIONS.md D1
  WakeWordRejected,    // backend said no, return to Idle silently

  // Backend asked for a specific face. data carries the
  // Expression enum value.
  SetExpression,

  // Round-trip liveness check.
  BackendPong,

  // Audio out - MAX98357A         PLANNED (not wired)
  AudioStarted,
  AudioFinished,

  // System
  NotificationReceived,
  AlarmTriggered,
  WifiConnected,
  WifiDisconnected,
  RtcError,
  SensorError,

  // Spotify                       PLANNED
  SpotifyState,
};

struct Event {
  EventType type = EventType::None;
  int32_t data = 0;   // event-specific; distance in mm, error code, etc.
};

class EventBus {
 public:
  static constexpr uint8_t kQueueSize = 16;

  // Returns false if the queue is full. Callers that
  // cannot tolerate loss must check.
  bool publish(EventType type, int32_t data = 0) {
    const uint8_t next = static_cast<uint8_t>((head_ + 1) % kQueueSize);
    if (next == tail_) {
      dropped_++;
      return false;
    }
    // Assigned field-wise rather than brace-initialised:
    // Event carries default member initialisers, which
    // makes aggregate initialisation illegal before C++14.
    queue_[head_].type = type;
    queue_[head_].data = data;
    head_ = next;
    return true;
  }

  bool poll(Event& out) {
    if (tail_ == head_) return false;
    out = queue_[tail_];
    tail_ = static_cast<uint8_t>((tail_ + 1) % kQueueSize);
    return true;
  }

  bool isEmpty() const { return head_ == tail_; }
  uint16_t droppedCount() const { return dropped_; }

 private:
  Event queue_[kQueueSize];
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
  uint16_t dropped_ = 0;
};

const char* eventName(EventType type);
