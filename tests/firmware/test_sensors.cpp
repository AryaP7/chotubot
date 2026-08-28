// ProximitySensor, BatteryMonitor and ExpressionMap.
//
// Hysteresis, curve interpolation and the expression
// allowlist -- all deterministic, all executing for real.

#include "support/fixture.h"
#include "support/tinytest.h"

#include "src/Config.h"
#include "src/core/Events.h"
#include "src/display/ExpressionMap.h"
#include "src/power/BatteryMonitor.h"
#include "src/sensors/ProximitySensor.h"

// ---------------------------------------------------------
// Proximity
// ---------------------------------------------------------

namespace {

struct Tof {
  ProximitySensor sensor;
  EventBus bus;

  Tof() {
    hostsim::resetAll();
    CHECK(sensor.begin());
  }

  // Hold a distance for N sample intervals.
  void hold(uint16_t mm, int samples) {
    hostsim::setTof(mm);
    for (int i = 0; i < samples; ++i) {
      hostsim::advanceMillis(Config::TOF_SAMPLE_INTERVAL_MS);
      sensor.update(millis(), bus);
    }
  }

  bool sawEvent(EventType wanted) {
    Event event;
    bool seen = false;
    while (bus.poll(event)) {
      if (event.type == wanted) seen = true;
    }
    return seen;
  }
};

}  // namespace

TEST(Proximity, absent_sensor_fails_cleanly) {
  hostsim::resetAll();
  hostsim::setTofPresent(false);

  ProximitySensor sensor;
  CHECK(!sensor.begin());
  CHECK(!sensor.isReady());
}

TEST(Proximity, starts_with_nobody_there) {
  Tof rig;
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::NoPerson));
}

TEST(Proximity, someone_walking_up_is_detected) {
  Tof rig;

  rig.hold(600, 5);   // inside the approach band
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::Approaching));

  rig.hold(200, 5);   // right up close
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::Nearby));
  CHECK(rig.sawEvent(EventType::PersonDetected));
}

TEST(Proximity, a_single_stray_reading_is_ignored) {
  Tof rig;
  rig.hold(1500, 5);

  // One frame of noise, fewer than TOF_CONFIRM_SAMPLES.
  rig.hold(200, 1);
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::NoPerson));
}

TEST(Proximity, hysteresis_stops_flapping_at_the_boundary) {
  Tof rig;
  rig.hold(200, 5);   // Nearby

  // Just past the ENTER threshold but inside EXIT: it must
  // stay Nearby rather than oscillate.
  rig.hold(Config::TOF_NEARBY_ENTER_MM + 20, 5);
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::Nearby));

  // Past EXIT: now it may leave.
  rig.hold(Config::TOF_NEARBY_EXIT_MM + 50, 5);
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::Approaching));
}

TEST(Proximity, leaving_passes_through_left_then_settles) {
  Tof rig;
  rig.hold(200, 5);
  CHECK(rig.sawEvent(EventType::PersonDetected));

  rig.hold(1600, 5);
  CHECK(rig.sawEvent(EventType::PersonLeft));

  // Left is transient - it collapses rather than sticking.
  rig.hold(1600, 3);
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::NoPerson));
}

TEST(Proximity, departure_is_announced_once) {
  Tof rig;
  rig.hold(200, 5);
  while (rig.sawEvent(EventType::PersonDetected)) {}

  rig.hold(1600, 10);

  int departures = 0;
  Event event;
  while (rig.bus.poll(event)) {
    if (event.type == EventType::PersonLeft) ++departures;
  }
  CHECK_EQ(departures, 1);
}

TEST(Proximity, out_of_range_reads_as_nobody) {
  Tof rig;
  rig.hold(Config::TOF_MAX_VALID_MM + 500, 5);

  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::NoPerson));
  CHECK_EQ(rig.sensor.lastDistanceMm(), 0);   // never fabricated
}

TEST(Proximity, sensor_timeout_reports_an_error_and_carries_on) {
  Tof rig;
  hostsim::setTofTimeout(true);

  rig.hold(200, 3);
  CHECK(rig.sawEvent(EventType::SensorError));
  CHECK_EQ(static_cast<int>(rig.sensor.state()),
           static_cast<int>(PresenceState::NoPerson));
}

TEST(Proximity, gestures_are_off_until_asked_for) {
  Tof rig;
  CHECK(!rig.sensor.gesturesEnabled());

  rig.hold(1200, 3);
  rig.hold(150, 3);   // a fast approach

  CHECK(!rig.sawEvent(EventType::GestureApproach));
}

TEST(Proximity, enabled_gestures_see_a_fast_approach) {
  Tof rig;
  rig.sensor.setGesturesEnabled(true);
  CHECK(rig.sensor.gesturesEnabled());

  rig.hold(1000, 2);
  rig.hold(300, 2);

  CHECK(rig.sawEvent(EventType::GestureApproach));
}

TEST(Proximity, every_state_has_a_name) {
  CHECK_STR_EQ(presenceName(PresenceState::NoPerson), "NO_PERSON");
  CHECK_STR_EQ(presenceName(PresenceState::Approaching), "APPROACHING");
  CHECK_STR_EQ(presenceName(PresenceState::Nearby), "NEARBY");
  CHECK_STR_EQ(presenceName(PresenceState::Left), "LEFT");
}

// ---------------------------------------------------------
// Battery
// ---------------------------------------------------------

namespace {

uint16_t readAt(uint32_t cellMilliVolts) {
  hostsim::resetAll();
  // The divider halves the cell, so the pin sees half.
  hostsim::setBatteryCellMilliVolts(cellMilliVolts);

  BatteryMonitor battery;
  battery.begin();
  hostsim::advanceMillis(10000);

  EventBus bus;
  battery.update(millis(), bus);
  return battery.millivolts();
}

uint8_t percentAt(uint32_t cellMilliVolts) {
  hostsim::resetAll();
  hostsim::setBatteryCellMilliVolts(cellMilliVolts);

  BatteryMonitor battery;
  battery.begin();
  hostsim::advanceMillis(10000);

  EventBus bus;
  battery.update(millis(), bus);
  return battery.percentEstimate();
}

}  // namespace

TEST(Battery, floating_pin_reports_nothing_rather_than_a_number) {
  // No divider soldered yet: the pin floats near zero.
  CHECK_EQ(readAt(0), 0);
  CHECK_EQ(percentAt(0), 0);
}

TEST(Battery, implausible_high_reading_is_rejected) {
  CHECK_EQ(readAt(6000), 0);
}

TEST(Battery, plausible_cell_voltage_is_reported) {
  const uint16_t mv = readAt(3800);
  CHECK(mv > 3700);
  CHECK(mv < 3900);
}

TEST(Battery, percentage_follows_the_discharge_curve) {
  CHECK_EQ(percentAt(4200), 100);
  CHECK(percentAt(3000) <= 1);

  // The flat middle of the curve must not read as 50% of
  // the voltage span - that is the whole point of the
  // piecewise table.
  const uint8_t mid = percentAt(3820);
  CHECK(mid >= 45);
  CHECK(mid <= 55);
}

TEST(Battery, percentage_is_monotonic) {
  uint8_t previous = 0;
  for (uint32_t mv = 3000; mv <= 4200; mv += 50) {
    const uint8_t pct = percentAt(mv);
    CHECK(pct >= previous);
    previous = pct;
  }
  CHECK_EQ(previous, 100);
}

TEST(Battery, low_cell_raises_one_warning) {
  hostsim::resetAll();
  hostsim::setBatteryCellMilliVolts(3300);

  BatteryMonitor battery;
  battery.begin();
  EventBus bus;

  for (int i = 0; i < 5; ++i) {
    hostsim::advanceMillis(6000);
    battery.update(millis(), bus);
  }

  int warnings = 0;
  Event event;
  while (bus.poll(event)) {
    if (event.type == EventType::BatteryLow) ++warnings;
  }
  CHECK_EQ(warnings, 1);
}

// ---------------------------------------------------------
// Expressions
// ---------------------------------------------------------

TEST(Expressions, every_expression_maps_to_a_name) {
  const Expression all[] = {
      Expression::Idle,     Expression::Listening, Expression::Thinking,
      Expression::Speaking, Expression::Happy,     Expression::Excited,
      Expression::Confused, Expression::Angry,     Expression::Sleepy,
      Expression::Love,     Expression::Surprised, Expression::Notification,
      Expression::Error,    Expression::Music,     Expression::Boot,
  };

  for (Expression expression : all) {
    CHECK(animationFor(expression) != nullptr);
    CHECK(strcmp(expressionName(expression), "?") != 0);
  }
}

TEST(Expressions, parsing_is_case_insensitive) {
  Expression parsed;

  CHECK(expressionFromName("happy", parsed));
  CHECK_EQ(static_cast<int>(parsed), static_cast<int>(Expression::Happy));

  CHECK(expressionFromName("HAPPY", parsed));
  CHECK_EQ(static_cast<int>(parsed), static_cast<int>(Expression::Happy));
}

TEST(Expressions, unknown_name_leaves_the_target_untouched) {
  Expression parsed = Expression::Angry;

  CHECK(!expressionFromName("wizard", parsed));
  CHECK_EQ(static_cast<int>(parsed), static_cast<int>(Expression::Angry));

  CHECK(!expressionFromName("", parsed));
  CHECK(!expressionFromName(nullptr, parsed));
  CHECK_EQ(static_cast<int>(parsed), static_cast<int>(Expression::Angry));
}

TEST(Expressions, round_trip_through_name_and_back) {
  const Expression all[] = {
      Expression::Idle,  Expression::Happy, Expression::Angry,
      Expression::Music, Expression::Boot,  Expression::Error,
  };

  for (Expression expression : all) {
    std::string lower = expressionName(expression);
    for (char& c : lower) c = static_cast<char>(tolower(c));

    Expression parsed;
    CHECK(expressionFromName(lower.c_str(), parsed));
    CHECK_EQ(static_cast<int>(parsed), static_cast<int>(expression));
  }
}
