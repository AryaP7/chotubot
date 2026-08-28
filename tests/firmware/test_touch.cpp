// TouchInput - the real gesture recogniser, on a fake clock.
//
// Time never actually passes here: every test steps the
// clock explicitly, so timing boundaries are exact rather
// than approximate.

#include "support/fixture.h"
#include "support/tinytest.h"

#include "src/Config.h"
#include "src/input/TouchInput.h"

namespace {

struct Touch {
  TouchInput input;

  Touch() {
    hostsim::resetAll();
    input.begin();
  }

  void press() { hostsim::setPin(Config::PIN_TOUCH, HIGH); }
  void release() { hostsim::setPin(Config::PIN_TOUCH, LOW); }

  // Step time in small slices, polling as the firmware
  // would in loop(). Returns the first real event seen.
  TouchEvent run(uint32_t durationMs, uint32_t stepMs = 5) {
    TouchEvent seen = TouchEvent::None;
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += stepMs) {
      hostsim::advanceMillis(stepMs);
      const TouchEvent event = input.update(millis());
      if (event != TouchEvent::None && seen == TouchEvent::None) seen = event;
    }
    return seen;
  }

  // One complete tap: hold, release, then settle.
  TouchEvent tap(uint32_t holdMs = 60) {
    press();
    TouchEvent seen = run(holdMs);
    release();
    const TouchEvent after = run(60);
    return (seen != TouchEvent::None) ? seen : after;
  }
};

}  // namespace

TEST(Touch, idle_produces_nothing) {
  Touch t;
  CHECK_EQ(static_cast<int>(t.run(500)), static_cast<int>(TouchEvent::None));
}

TEST(Touch, single_tap_resolves_to_short_after_the_window) {
  Touch t;

  t.press();
  t.run(60);
  t.release();

  // Must NOT fire immediately - a second tap could still
  // arrive and make this a double.
  CHECK_EQ(static_cast<int>(t.run(Config::TOUCH_MULTI_WINDOW_MS - 100)),
           static_cast<int>(TouchEvent::None));

  CHECK_EQ(static_cast<int>(t.run(300)), static_cast<int>(TouchEvent::Short));
}

TEST(Touch, tap_fires_on_release_not_on_press) {
  Touch t;

  t.press();
  // Held, but well under the long-press threshold.
  CHECK_EQ(static_cast<int>(t.run(300)), static_cast<int>(TouchEvent::None));

  t.release();
  CHECK_EQ(static_cast<int>(t.run(500)), static_cast<int>(TouchEvent::Short));
}

TEST(Touch, double_tap_inside_the_window) {
  Touch t;

  t.press();
  t.run(50);
  t.release();
  t.run(80);          // inside TOUCH_MULTI_WINDOW_MS

  t.press();
  t.run(50);
  t.release();

  CHECK_EQ(static_cast<int>(t.run(500)), static_cast<int>(TouchEvent::Double));
}

TEST(Touch, two_taps_outside_the_window_are_two_shorts) {
  Touch t;

  CHECK_EQ(static_cast<int>(t.tap()), static_cast<int>(TouchEvent::None));
  CHECK_EQ(static_cast<int>(t.run(400)), static_cast<int>(TouchEvent::Short));

  CHECK_EQ(static_cast<int>(t.tap()), static_cast<int>(TouchEvent::None));
  CHECK_EQ(static_cast<int>(t.run(400)), static_cast<int>(TouchEvent::Short));
}

TEST(Touch, triple_tap_resolves_immediately) {
  Touch t;

  for (int i = 0; i < 2; ++i) {
    t.press();
    t.run(50);
    t.release();
    t.run(80);
  }

  t.press();
  t.run(50);
  t.release();

  // Nothing can extend a triple, so it does not wait.
  CHECK_EQ(static_cast<int>(t.run(60)), static_cast<int>(TouchEvent::Triple));
}

TEST(Touch, long_press_fires_while_still_held) {
  Touch t;
  t.press();

  // Before the threshold: nothing.
  CHECK_EQ(static_cast<int>(t.run(Config::TOUCH_LONG_MS - 200)),
           static_cast<int>(TouchEvent::None));

  // Crossing it fires without waiting for release.
  CHECK_EQ(static_cast<int>(t.run(400)), static_cast<int>(TouchEvent::Long));
  CHECK(t.input.isHeld());
}

TEST(Touch, long_press_does_not_also_count_as_a_tap) {
  Touch t;
  t.press();
  t.run(Config::TOUCH_LONG_MS + 200);   // Long already fired

  t.release();
  CHECK_EQ(static_cast<int>(t.run(600)), static_cast<int>(TouchEvent::None));
}

TEST(Touch, contact_bounce_shorter_than_debounce_is_ignored) {
  Touch t;

  // Flap the line faster than TOUCH_DEBOUNCE_MS.
  for (int i = 0; i < 6; ++i) {
    hostsim::setPin(Config::PIN_TOUCH, (i % 2) ? HIGH : LOW);
    hostsim::advanceMillis(5);
    CHECK_EQ(static_cast<int>(t.input.update(millis())),
             static_cast<int>(TouchEvent::None));
  }

  t.release();
  CHECK_EQ(static_cast<int>(t.run(600)), static_cast<int>(TouchEvent::None));
}

TEST(Touch, held_state_tracks_the_pin) {
  Touch t;
  CHECK(!t.input.isHeld());

  t.press();
  t.run(100);
  CHECK(t.input.isHeld());

  t.release();
  t.run(100);
  CHECK(!t.input.isHeld());
}

TEST(Touch, long_press_boundary_is_exact) {
  Touch t;
  t.press();

  // Step to one millisecond short of the threshold.
  hostsim::setMillis(0);
  t.input.update(0);
  hostsim::setMillis(Config::TOUCH_DEBOUNCE_MS + 1);
  t.input.update(millis());

  const uint32_t pressedAt = millis();

  hostsim::setMillis(pressedAt + Config::TOUCH_LONG_MS - 1);
  CHECK_EQ(static_cast<int>(t.input.update(millis())),
           static_cast<int>(TouchEvent::None));

  hostsim::setMillis(pressedAt + Config::TOUCH_LONG_MS);
  CHECK_EQ(static_cast<int>(t.input.update(millis())),
           static_cast<int>(TouchEvent::Long));
}

TEST(Touch, every_event_has_a_name) {
  CHECK_STR_EQ(touchEventName(TouchEvent::Short), "SHORT");
  CHECK_STR_EQ(touchEventName(TouchEvent::Double), "DOUBLE");
  CHECK_STR_EQ(touchEventName(TouchEvent::Long), "LONG");
  CHECK_STR_EQ(touchEventName(TouchEvent::Triple), "TRIPLE");
  CHECK_STR_EQ(touchEventName(TouchEvent::None), "NONE");
}
