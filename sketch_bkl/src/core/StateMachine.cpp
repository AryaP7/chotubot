#include "StateMachine.h"
#include "../display/ExpressionMap.h"
#include "../display/MochiPlayer.h"
#include "../lighting/LedController.h"

const char* botStateName(BotState state) {
  switch (state) {
    case BotState::Booting:          return "BOOTING";
    case BotState::Idle:             return "IDLE";
    case BotState::Clock:            return "CLOCK";
    case BotState::Diagnostics:      return "DIAGNOSTICS";
    case BotState::NowPlaying:       return "NOW_PLAYING";
    case BotState::PresenceDetected: return "PRESENCE";
    case BotState::Listening:        return "LISTENING";
    case BotState::Thinking:         return "THINKING";
    case BotState::Speaking:         return "SPEAKING";
    case BotState::Notification:     return "NOTIFICATION";
    case BotState::Sleep:            return "SLEEP";
    case BotState::Error:            return "ERROR";
    case BotState::Offline:          return "OFFLINE";
  }
  return "?";
}

namespace {

// Pin the display to one expression, or let it cycle.
// Idle deliberately cycles all 18 animations, which is
// exactly what the verified V1 build does.
struct StatePresentation {
  bool pinned;
  Expression expression;
};

StatePresentation presentationFor(BotState state) {
  switch (state) {
    case BotState::Idle:             return {false, Expression::Idle};
    case BotState::Booting:          return {true,  Expression::Boot};
    case BotState::PresenceDetected: return {true,  Expression::Happy};
    case BotState::Listening:        return {true,  Expression::Listening};
    case BotState::Thinking:         return {true,  Expression::Thinking};
    case BotState::Speaking:         return {true,  Expression::Speaking};
    case BotState::Notification:     return {true,  Expression::Notification};
    case BotState::Sleep:            return {true,  Expression::Sleepy};
    case BotState::Error:            return {true,  Expression::Error};
    case BotState::Offline:          return {true,  Expression::Confused};

    // Screens that do not render Mochi at all.
    case BotState::Clock:
    case BotState::Diagnostics:
    case BotState::NowPlaying:       return {false, Expression::Idle};
  }
  return {false, Expression::Idle};
}

bool rendersMochi(BotState state) {
  // Notification shows its text instead of a face - the
  // words are the point. The reaction still comes through
  // the LEDs and the audio cue.
  return state != BotState::Clock && state != BotState::Diagnostics &&
         state != BotState::NowPlaying && state != BotState::Notification;
}

// The lighting half of each state's definition.
LedMode ledModeFor(BotState state) {
  switch (state) {
    case BotState::Booting:          return LedMode::Thinking;
    case BotState::Idle:             return LedMode::Idle;
    case BotState::Clock:            return LedMode::Idle;
    case BotState::Diagnostics:      return LedMode::Idle;
    case BotState::NowPlaying:       return LedMode::Music;
    case BotState::PresenceDetected: return LedMode::Speaking;
    case BotState::Listening:        return LedMode::Listening;
    case BotState::Thinking:         return LedMode::Thinking;
    case BotState::Speaking:         return LedMode::Speaking;
    case BotState::Notification:     return LedMode::Notification;
    case BotState::Sleep:            return LedMode::Sleep;
    case BotState::Error:            return LedMode::Error;
    case BotState::Offline:          return LedMode::Error;
  }
  return LedMode::Idle;
}

}  // namespace

void StateMachine::begin(MochiPlayer& mochi, LedController& leds,
                         uint32_t nowMs) {
  mochi_ = &mochi;
  leds_ = &leds;
  state_ = BotState::Booting;
  resumeState_ = BotState::Idle;
  transitionTo(BotState::Idle, nowMs);
}

void StateMachine::onEnter(BotState next, uint32_t nowMs) {
  if (leds_) {
    leds_->setMode(ledModeFor(next));
  }

  if (!mochi_) return;

  const StatePresentation p = presentationFor(next);

  if (rendersMochi(next)) {
    mochi_->setAutoAdvance(!p.pinned);

    if (p.pinned) {
      // A missing animation must not silently pin the
      // wrong face - selectByName leaves the selection
      // alone and returns false.
      if (!mochi_->selectByName(animationFor(p.expression))) {
        Serial.print(F("[state] missing animation for "));
        Serial.println(expressionName(p.expression));
      }
    }

    mochi_->restart(nowMs);
  }
}

void StateMachine::transitionTo(BotState next, uint32_t nowMs) {
  if (next == state_) return;

  Serial.print(F("[state] "));
  Serial.print(botStateName(state_));
  Serial.print(F(" -> "));
  Serial.println(botStateName(next));

  // Remember where to come back to when leaving a
  // transient screen.
  if (rendersMochi(state_) && !rendersMochi(next)) {
    resumeState_ = state_;
  }

  state_ = next;
  onEnter(next, nowMs);
}

bool StateMachine::handle(const Event& event, uint32_t nowMs) {
  const BotState before = state_;

  switch (event.type) {
    // ---- Touch, per the gesture spec ----
    // Note this widens the V1 interaction: a single tap
    // used to be a two-way Mochi/Clock toggle, and now
    // cycles Mochi -> Clock -> Diagnostics. A double tap
    // is the direct route to the clock.
    case EventType::TouchShort:
      switch (state_) {
        case BotState::Clock:
          transitionTo(BotState::NowPlaying, nowMs);
          break;
        case BotState::NowPlaying:
          transitionTo(BotState::Diagnostics, nowMs);
          break;
        case BotState::Diagnostics:
          transitionTo(resumeState_, nowMs);
          break;
        default:
          transitionTo(BotState::Clock, nowMs);
          break;
      }
      break;

    case EventType::TouchDouble:
      transitionTo(state_ == BotState::Clock ? resumeState_ : BotState::Clock,
                   nowMs);
      break;

    case EventType::TouchLong:
      // Manual override for the assistant. Works even
      // with no backend - it just will not hear back.
      transitionTo(BotState::Listening, nowMs);
      break;

    case EventType::TouchTriple:
      transitionTo(state_ == BotState::Sleep ? BotState::Idle : BotState::Sleep,
                   nowMs);
      break;

    // ---- Time ----
    case EventType::AlarmFired:
      transitionTo(BotState::Notification, nowMs);
      break;

    case EventType::QuietHoursStarted:
      // Only drift off on its own if nothing is going on.
      if (state_ == BotState::Idle) {
        transitionTo(BotState::Sleep, nowMs);
      }
      break;

    case EventType::QuietHoursEnded:
      if (state_ == BotState::Sleep) {
        transitionTo(BotState::Idle, nowMs);
      }
      break;

    case EventType::BatteryLow:
      Serial.print(F("[state] battery low: "));
      Serial.print(event.data);
      Serial.println(F(" mV"));
      break;

    case EventType::RtcError:
      // Non-fatal. The clock screen degrades; the rest of
      // the bot carries on. See DECISIONS.md D5.
      Serial.println(F("[state] RTC unavailable"));
      break;

    // ---- Defined, not yet reachable ----
    // These arrive only once the relevant hardware and
    // the Wi-Fi layer exist. Listed so the routing is
    // written down, not to imply they fire today.
    case EventType::WakeWordConfirmed:
      transitionTo(BotState::Listening, nowMs);
      break;

    case EventType::WakeWordRejected:
      // Backend heard voice but no wake word. Return
      // quietly - the user should see nothing.
      transitionTo(resumeState_, nowMs);
      break;

    case EventType::AiRequest:
      transitionTo(BotState::Thinking, nowMs);
      break;

    case EventType::AudioStarted:
      transitionTo(BotState::Speaking, nowMs);
      break;

    case EventType::AudioFinished:
      transitionTo(resumeState_, nowMs);
      break;

    case EventType::AiError:
      transitionTo(BotState::Error, nowMs);
      break;

    case EventType::NotificationReceived:
      transitionTo(BotState::Notification, nowMs);
      break;

    case EventType::SetExpression:
      // Backend-driven face change. Only applies while a
      // Mochi-rendering state is on screen; it must not
      // yank the user off the clock or diagnostics.
      if (mochi_ && rendersMochi(state_)) {
        const Expression expression =
            static_cast<Expression>(event.data);
        mochi_->setExpression(expression, true);
        mochi_->restart(nowMs);

        Serial.print(F("[state] expression -> "));
        Serial.println(expressionName(expression));
      }
      break;

    case EventType::BackendPong:
      Serial.println(F("[backend] pong"));
      break;

    case EventType::SpotifyState:
      // Reflect playback in the face without stealing the
      // screen. event.data is 1 while playing.
      if (mochi_ && state_ == BotState::Idle) {
        mochi_->setExpression(
            event.data ? Expression::Music : Expression::Idle, event.data != 0);
      }
      if (leds_ && state_ == BotState::Idle) {
        leds_->setMode(event.data ? LedMode::Music : LedMode::Idle);
      }
      break;

    case EventType::PersonDetected:
      if (state_ == BotState::Idle) {
        transitionTo(BotState::PresenceDetected, nowMs);
      }
      break;

    case EventType::PersonLeft:
      if (state_ == BotState::PresenceDetected) {
        transitionTo(BotState::Idle, nowMs);
      }
      break;

    case EventType::WifiDisconnected:
      Serial.println(F("[state] wifi down - offline mode"));
      break;

    default:
      break;
  }

  return state_ != before;
}
