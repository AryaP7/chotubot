// StateMachine - the real firmware transition table.
//
// Only transitions that actually exist are asserted. Where
// the spec's expectation and the firmware differ, the
// firmware wins and the difference is documented in the
// test name.

#include "support/fixture.h"
#include "support/tinytest.h"

#include "src/core/Events.h"
#include "src/core/StateMachine.h"
#include "src/display/MochiPlayer.h"
#include "src/lighting/LedController.h"

namespace {

struct Rig {
  MochiPlayer mochi;
  LedController leds;
  StateMachine machine;
  EventBus bus;

  Rig() {
    hostsim::resetAll();
    mochi.begin();
    leds.begin();
    machine.begin(mochi, leds, millis());
  }

  void send(EventType type, int32_t data = 0) {
    bus.publish(type, data);

    Event event;
    while (bus.poll(event)) {
      machine.handle(event, millis());
    }
  }
};

}  // namespace

TEST(StateMachine, booting_settles_into_idle) {
  Rig rig;
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, idle_shows_mochi_and_cycles_all_animations) {
  Rig rig;
  // Idle must not pin one face - the verified V1 build
  // cycles the whole table.
  CHECK(rig.mochi.autoAdvance());
}

TEST(StateMachine, short_touch_idle_to_clock) {
  Rig rig;
  rig.send(EventType::TouchShort);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Clock));
}

TEST(StateMachine, short_touch_cycles_four_screens_not_a_toggle) {
  // This is the behaviour STATUS.md documents. A single tap
  // does NOT return to Mochi from the clock.
  Rig rig;

  rig.send(EventType::TouchShort);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Clock));

  rig.send(EventType::TouchShort);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::NowPlaying));

  rig.send(EventType::TouchShort);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Diagnostics));

  rig.send(EventType::TouchShort);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, double_touch_is_the_clock_toggle) {
  Rig rig;

  rig.send(EventType::TouchDouble);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Clock));

  // Clock -> Mochi, which is what the spec calls CLOCK -> MOCHI.
  rig.send(EventType::TouchDouble);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, long_touch_enters_listening) {
  Rig rig;
  rig.send(EventType::TouchLong);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Listening));
}

TEST(StateMachine, triple_touch_toggles_sleep) {
  Rig rig;

  rig.send(EventType::TouchTriple);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Sleep));

  rig.send(EventType::TouchTriple);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, full_assistant_round_trip) {
  Rig rig;

  rig.send(EventType::WakeWordConfirmed);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Listening));

  rig.send(EventType::AiRequest);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Thinking));

  rig.send(EventType::AudioStarted);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Speaking));

  rig.send(EventType::AudioFinished);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, rejected_wake_word_returns_quietly) {
  Rig rig;
  rig.send(EventType::WakeWordRejected);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, ai_error_enters_error_state) {
  Rig rig;
  rig.send(EventType::AiError);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Error));
}

TEST(StateMachine, offline_state_exists_but_nothing_reaches_it) {
  Rig rig;

  // WifiDisconnected only logs today - it does NOT transition.
  rig.send(EventType::WifiDisconnected);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));

  // The state itself is reachable programmatically.
  rig.machine.transitionTo(BotState::Offline, millis());
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Offline));
}

TEST(StateMachine, missing_rtc_does_not_change_state) {
  Rig rig;
  rig.send(EventType::RtcError);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, presence_only_interrupts_idle) {
  Rig rig;

  rig.send(EventType::PersonDetected, 250);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::PresenceDetected));

  rig.send(EventType::PersonLeft);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, presence_does_not_interrupt_the_clock) {
  Rig rig;
  rig.send(EventType::TouchShort);   // -> Clock

  rig.send(EventType::PersonDetected, 250);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Clock));
}

TEST(StateMachine, notification_interrupts_and_alarm_does_too) {
  Rig rig;

  rig.send(EventType::NotificationReceived);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Notification));

  rig.machine.transitionTo(BotState::Idle, millis());
  rig.send(EventType::AlarmFired, 2);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Notification));
}

TEST(StateMachine, quiet_hours_only_sleep_from_idle) {
  Rig rig;

  rig.send(EventType::QuietHoursStarted);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Sleep));

  rig.send(EventType::QuietHoursEnded);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Idle));
}

TEST(StateMachine, quiet_hours_do_not_interrupt_listening) {
  Rig rig;
  rig.send(EventType::TouchLong);   // -> Listening

  rig.send(EventType::QuietHoursStarted);
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Listening));
}

TEST(StateMachine, backend_expression_pins_a_face) {
  Rig rig;

  rig.send(EventType::SetExpression,
           static_cast<int32_t>(Expression::Angry));

  CHECK_STR_EQ(rig.mochi.currentName(), "angry");
  // Pinned, so it stops cycling the table.
  CHECK(!rig.mochi.autoAdvance());
}

TEST(StateMachine, backend_expression_ignored_on_a_non_mochi_screen) {
  Rig rig;
  rig.send(EventType::TouchShort);   // -> Clock

  const char* before = rig.mochi.currentName();
  rig.send(EventType::SetExpression,
           static_cast<int32_t>(Expression::Angry));

  // The clock must not be yanked away by the backend.
  CHECK_EQ(static_cast<int>(rig.machine.state()),
           static_cast<int>(BotState::Clock));
  CHECK_STR_EQ(rig.mochi.currentName(), before);
}

TEST(StateMachine, every_state_has_a_name) {
  const BotState all[] = {
      BotState::Booting,   BotState::Idle,        BotState::Clock,
      BotState::Diagnostics, BotState::NowPlaying, BotState::PresenceDetected,
      BotState::Listening, BotState::Thinking,    BotState::Speaking,
      BotState::Notification, BotState::Sleep,    BotState::Error,
      BotState::Offline,
  };

  for (BotState state : all) {
    CHECK(strcmp(botStateName(state), "?") != 0);
  }
}

TEST(StateMachine, redundant_transition_is_a_no_op) {
  Rig rig;
  hostsim::clearSerial();

  rig.machine.transitionTo(BotState::Idle, millis());

  // Already Idle, so nothing should have been logged.
  CHECK_EQ(hostsim::serialLog().size(), 0u);
}
