#pragma once
#include <Arduino.h>

class EventBus;

// =====================================================
// MICROPHONE - INMP441 on I2S1
//
// Captures 16 kHz mono audio and decides, locally,
// whether anyone is talking.
//
// WHAT THIS IS FOR
// The VAD here exists for exactly one reason: so the bot
// does not stream silence over Wi-Fi all day. It is an
// energy gate, not recognition.
//
// It does NOT detect a wake word. It cannot tell speech
// from a slammed door. The backend decides whether the
// audio contained "Hey Jarvis". See DECISIONS.md D1.
//
// STATUS: IMPLEMENTED, hardware UNVERIFIED.
// =====================================================

class Microphone {
 public:
  bool begin();

  // Non-blocking. Reads at most one block per call and
  // publishes VoiceStarted / VoiceStopped.
  void update(uint32_t nowMs, EventBus& bus);

  bool isReady() const { return ready_; }
  bool isVoiceDetected() const { return voiceActive_; }

  // RMS of the most recent block, and the adaptive noise
  // floor it is judged against. Both are real measured
  // values - they read 0 until the mic actually runs.
  uint16_t level() const { return lastRms_; }
  uint16_t noiseFloor() const { return noiseFloor_; }

  // Most recent block as 16-bit mono. Valid until the
  // next update(). Returns sample count, 0 if none.
  //
  // This is the handoff point for the Wi-Fi layer: while
  // isVoiceDetected() is true, these blocks are what get
  // shipped to the backend.
  uint16_t readAudio(const int16_t*& outSamples) const;

 private:
  // Starting guess for the noise floor. It adapts to the
  // real room within a second or so of running.
  static constexpr uint16_t kInitialNoiseFloor = 400;

  bool readBlock();

  bool ready_ = false;
  bool voiceActive_ = false;

  uint8_t attackCount_ = 0;
  uint8_t hangoverCount_ = 0;

  uint16_t lastRms_ = 0;
  uint16_t noiseFloor_ = kInitialNoiseFloor;
  uint16_t validSamples_ = 0;
};
