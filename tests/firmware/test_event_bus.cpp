// EventBus - the real firmware ring buffer, executing.

#include "support/tinytest.h"

#include "src/core/Events.h"

TEST(EventBus, starts_empty) {
  EventBus bus;
  Event event;

  CHECK(bus.isEmpty());
  CHECK(!bus.poll(event));
  CHECK_EQ(bus.droppedCount(), 0);
}

TEST(EventBus, publish_then_poll_round_trip) {
  EventBus bus;

  CHECK(bus.publish(EventType::TouchShort, 42));
  CHECK(!bus.isEmpty());

  Event event;
  CHECK(bus.poll(event));
  CHECK_EQ(static_cast<int>(event.type), static_cast<int>(EventType::TouchShort));
  CHECK_EQ(event.data, 42);
  CHECK(bus.isEmpty());
}

TEST(EventBus, preserves_order) {
  EventBus bus;

  bus.publish(EventType::TouchShort, 1);
  bus.publish(EventType::TouchDouble, 2);
  bus.publish(EventType::TouchLong, 3);

  Event event;
  bus.poll(event);
  CHECK_EQ(event.data, 1);
  bus.poll(event);
  CHECK_EQ(event.data, 2);
  bus.poll(event);
  CHECK_EQ(event.data, 3);
  CHECK(bus.isEmpty());
}

TEST(EventBus, default_payload_is_zero) {
  EventBus bus;
  bus.publish(EventType::WifiConnected);

  Event event;
  bus.poll(event);
  CHECK_EQ(event.data, 0);
}

TEST(EventBus, carries_negative_and_large_payloads) {
  EventBus bus;
  bus.publish(EventType::BatteryLow, -1);
  bus.publish(EventType::PersonDetected, 2000000000);

  Event event;
  bus.poll(event);
  CHECK_EQ(event.data, -1);
  bus.poll(event);
  CHECK_EQ(event.data, 2000000000);
}

TEST(EventBus, full_queue_drops_and_counts) {
  EventBus bus;

  // The ring keeps one slot free to tell full from empty,
  // so capacity is kQueueSize - 1.
  const int capacity = EventBus::kQueueSize - 1;

  for (int i = 0; i < capacity; ++i) {
    CHECK(bus.publish(EventType::TouchShort, i));
  }

  CHECK(!bus.publish(EventType::TouchShort, 999));
  CHECK_EQ(bus.droppedCount(), 1);

  bus.publish(EventType::TouchShort, 999);
  CHECK_EQ(bus.droppedCount(), 2);
}

TEST(EventBus, draining_makes_room_again) {
  EventBus bus;
  const int capacity = EventBus::kQueueSize - 1;

  for (int i = 0; i < capacity; ++i) bus.publish(EventType::TouchShort, i);

  Event event;
  bus.poll(event);
  CHECK_EQ(event.data, 0);

  // One slot freed, so one more fits.
  CHECK(bus.publish(EventType::TouchShort, 100));
}

TEST(EventBus, wraps_around_many_times) {
  EventBus bus;
  Event event;

  // Far more traffic than the ring holds, one at a time.
  for (int i = 0; i < 200; ++i) {
    CHECK(bus.publish(EventType::PersonDetected, i));
    CHECK(bus.poll(event));
    CHECK_EQ(event.data, i);
  }

  CHECK_EQ(bus.droppedCount(), 0);
}

TEST(EventBus, every_event_type_has_a_name) {
  // Guards against a new enum value being added without a
  // matching case in eventName().
  const EventType all[] = {
      EventType::TouchShort,     EventType::TouchDouble,
      EventType::TouchLong,      EventType::TouchTriple,
      EventType::PersonDetected, EventType::PersonLeft,
      EventType::GestureApproach, EventType::GestureHover,
      EventType::VoiceStarted,   EventType::VoiceStopped,
      EventType::AiRequest,      EventType::AiError,
      EventType::WakeWordConfirmed, EventType::WakeWordRejected,
      EventType::SetExpression,  EventType::BackendPong,
      EventType::AudioStarted,   EventType::AudioFinished,
      EventType::AlarmFired,     EventType::BatteryLow,
      EventType::WifiConnected,  EventType::WifiDisconnected,
  };

  for (EventType type : all) {
    const char* name = eventName(type);
    CHECK(name != nullptr);
    CHECK_STR_EQ(name, name);  // not "UNKNOWN"
    CHECK(strcmp(name, "UNKNOWN") != 0);
  }
}
