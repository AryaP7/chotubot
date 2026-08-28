// Microphone VAD - the real energy gate.
//
// The I2S driver is stubbed, so readBlock(), the RMS
// maths, the adaptive noise floor and the hysteresis all
// run exactly as written. Nothing here proves an INMP441
// exists.

#include "support/fixture.h"
#include "support/tinytest.h"

#include "src/Config.h"
#include "src/audio/Microphone.h"
#include "src/core/Events.h"

namespace {

struct Rig {
  Microphone mic;
  EventBus bus;

  Rig() {
    hostsim::resetAll();
    CHECK(mic.begin());
  }

  // Feed N blocks at a given amplitude and pump update().
  void feed(int blocks, int16_t amplitude) {
    for (int i = 0; i < blocks; ++i) {
      hostsim::queueMicTone(Config::MIC_BLOCK_SAMPLES, amplitude);
      hostsim::advanceMillis(16);
      mic.update(millis(), bus);
    }
  }

  int countEvents(EventType wanted) {
    int seen = 0;
    Event event;
    while (bus.poll(event)) {
      if (event.type == wanted) ++seen;
    }
    return seen;
  }

  bool sawEvent(EventType wanted) { return countEvents(wanted) > 0; }
};

}  // namespace

TEST(VAD, begin_fails_cleanly_when_i2s_will_not_start) {
  hostsim::resetAll();
  hostsim::setMicBegin(false);

  Microphone mic;
  CHECK(!mic.begin());
  CHECK(!mic.isReady());
}

TEST(VAD, silence_never_triggers) {
  Rig rig;
  rig.feed(40, 0);

  CHECK(!rig.mic.isVoiceDetected());
  CHECK(!rig.sawEvent(EventType::VoiceStarted));
}

TEST(VAD, rms_reflects_the_signal) {
  Rig rig;

  rig.feed(1, 0);
  CHECK_EQ(rig.mic.level(), 0);

  rig.feed(1, 8000);
  // A square wave at amplitude A has RMS A.
  CHECK(rig.mic.level() > 7000);
  CHECK(rig.mic.level() < 9000);
}

TEST(VAD, quiet_room_noise_does_not_trigger) {
  Rig rig;
  // Below VAD_MIN_RMS, so it can never pass the gate no
  // matter where the adaptive floor has drifted.
  rig.feed(40, 100);

  CHECK(!rig.mic.isVoiceDetected());
  CHECK(!rig.sawEvent(EventType::VoiceStarted));
}

TEST(VAD, speech_level_audio_starts_a_segment) {
  Rig rig;
  rig.feed(10, 0);        // settle the noise floor
  rig.feed(5, 6000);      // talk

  CHECK(rig.mic.isVoiceDetected());
  CHECK(rig.sawEvent(EventType::VoiceStarted));
}

TEST(VAD, attack_requires_more_than_one_block) {
  Rig rig;
  rig.feed(10, 0);

  // A single loud block is a door slam, not speech.
  rig.feed(1, 6000);
  CHECK(!rig.mic.isVoiceDetected());

  rig.feed(1, 6000);
  CHECK(rig.mic.isVoiceDetected());
}

TEST(VAD, segment_survives_a_brief_pause_then_closes) {
  Rig rig;
  rig.feed(10, 0);
  rig.feed(4, 6000);
  CHECK(rig.mic.isVoiceDetected());

  // A short gap must not chop the segment - the hangover
  // window keeps it open.
  rig.feed(5, 0);
  CHECK(rig.mic.isVoiceDetected());

  // Long enough silence and it closes.
  rig.feed(Config::VAD_HANGOVER_BLOCKS + 5, 0);
  CHECK(!rig.mic.isVoiceDetected());
  CHECK(rig.sawEvent(EventType::VoiceStopped));
}

TEST(VAD, hangover_length_matches_configuration) {
  Rig rig;
  rig.feed(10, 0);
  rig.feed(4, 6000);
  CHECK(rig.mic.isVoiceDetected());

  // One block short of the hangover: still open.
  rig.feed(Config::VAD_HANGOVER_BLOCKS, 0);
  CHECK(rig.mic.isVoiceDetected());

  rig.feed(2, 0);
  CHECK(!rig.mic.isVoiceDetected());
}

TEST(VAD, noise_floor_adapts_upward_while_quiet) {
  Rig rig;
  const uint16_t initial = rig.mic.noiseFloor();

  // Steady quiet hiss, below the trigger.
  rig.feed(60, 150);

  CHECK_NE(rig.mic.noiseFloor(), initial);
  CHECK(!rig.mic.isVoiceDetected());
}

TEST(VAD, floor_does_not_climb_during_speech) {
  Rig rig;
  rig.feed(20, 0);
  const uint16_t quietFloor = rig.mic.noiseFloor();

  rig.feed(20, 9000);   // sustained speech
  CHECK(rig.mic.isVoiceDetected());

  // Adapting during speech would deafen the gate.
  CHECK(rig.mic.noiseFloor() <= quietFloor + 50);
}

TEST(VAD, readAudio_exposes_the_last_block) {
  Rig rig;
  rig.feed(1, 4000);

  const int16_t* samples = nullptr;
  const uint16_t count = rig.mic.readAudio(samples);

  CHECK_EQ(count, Config::MIC_BLOCK_SAMPLES);
  CHECK(samples != nullptr);
  CHECK_EQ(samples[0], 4000);
  CHECK_EQ(samples[1], -4000);
}

TEST(VAD, readAudio_is_empty_before_any_capture) {
  hostsim::resetAll();
  Microphone mic;
  mic.begin();

  const int16_t* samples = nullptr;
  CHECK_EQ(mic.readAudio(samples), 0);
  CHECK(samples == nullptr);
}

TEST(VAD, update_is_a_no_op_with_no_data_queued) {
  Rig rig;
  const uint16_t before = rig.mic.level();

  hostsim::advanceMillis(100);
  rig.mic.update(millis(), rig.bus);

  CHECK_EQ(rig.mic.level(), before);
}

TEST(VAD, only_one_start_event_per_segment) {
  Rig rig;
  rig.feed(10, 0);
  rig.feed(20, 6000);

  CHECK_EQ(rig.countEvents(EventType::VoiceStarted), 1);
}
