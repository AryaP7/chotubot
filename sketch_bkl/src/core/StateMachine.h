#pragma once
#include <Arduino.h>
#include "Events.h"

class MochiPlayer;
class LedController;

// =====================================================
// BOT STATE MACHINE
//
// The single owner of "what the bot is doing". Modules
// publish events; only this decides what the display,
// and later the LEDs and audio, should be doing.
//
// PHASE 2 REACHABILITY - honest accounting:
//   Booting     entered at setup
//   Idle        reachable
//   Clock       reachable  (TouchShort)
//   Diagnostics reachable  (serial 'd')
//   Everything below is DEFINED but UNREACHABLE until
//   the hardware and Wi-Fi layers land. Nothing
//   pretends otherwise.
// =====================================================

enum class BotState : uint8_t {
  Booting = 0,
  Idle,
  Clock,
  Diagnostics,
  NowPlaying,

  PresenceDetected,   // PLANNED - needs VL53L0X
  Listening,          // PLANNED - needs INMP441 + VAD
  Thinking,           // PLANNED - needs backend
  Speaking,           // PLANNED - needs MAX98357A
  Notification,       // PLANNED - needs backend
  Sleep,              // PLANNED
  Error,
  Offline,            // PLANNED - needs Wi-Fi
};

const char* botStateName(BotState state);

class StateMachine {
 public:
  void begin(MochiPlayer& mochi, LedController& leds, uint32_t nowMs);

  // Consume one event. Returns true if the state changed.
  bool handle(const Event& event, uint32_t nowMs);

  void transitionTo(BotState next, uint32_t nowMs);

  BotState state() const { return state_; }

 private:
  void onEnter(BotState next, uint32_t nowMs);

  MochiPlayer* mochi_ = nullptr;
  LedController* leds_ = nullptr;
  BotState state_ = BotState::Booting;

  // Where a short touch returns to when leaving Clock or
  // Diagnostics. Idle in practice, kept explicit so
  // later states can be returned to correctly.
  BotState resumeState_ = BotState::Idle;
};
