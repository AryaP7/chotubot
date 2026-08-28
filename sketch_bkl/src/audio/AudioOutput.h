#pragma once
#include <Arduino.h>

class EventBus;

// =====================================================
// AUDIO OUTPUT - MAX98357A on I2S0
//
// The amplifier's SD pin is driven from a GPIO rather
// than strapped with a resistor: HIGH selects the left
// channel and enables the amp, LOW is a genuine hardware
// shutdown. That gives a real mute with no idle hiss and
// no current draw between sounds.
//
// WHAT WORKS TODAY
//   playTone()  - generated on-device, non-blocking
//   writePcm()  - push 16-bit mono from anywhere
//
// WHAT DOES NOT EXIST YET
//   Streaming TTS from the backend. writePcm() is the
//   seam it will use, but nothing feeds it until the
//   Wi-Fi layer lands.
//
// STATUS: IMPLEMENTED, hardware UNVERIFIED.
// =====================================================

class AudioOutput {
 public:
  bool begin();

  // Non-blocking. Feeds the I2S DMA in small chunks.
  void update(uint32_t nowMs, EventBus& bus);

  // Square-wave test tone. Its only job is proving the
  // amplifier and speaker are alive at stage 08.
  void playTone(uint16_t frequencyHz, uint16_t durationMs);

  // Push mono 16-bit PCM at AUDIO_SAMPLE_RATE. Returns
  // samples accepted, which may be fewer than offered.
  size_t writePcm(const int16_t* samples, size_t count);

  void stop();

  bool isReady() const { return ready_; }
  bool isPlaying() const { return playing_; }

  // 0-100. Applied in software before the samples go out.
  void setVolume(uint8_t percent);
  uint8_t volume() const { return volume_; }

 private:
  void setAmplifierEnabled(bool enabled);
  void emitToneChunk();

  bool ready_ = false;
  bool playing_ = false;
  uint8_t volume_ = 0;

  // Tone generator state.
  uint32_t toneSamplesLeft_ = 0;
  uint16_t toneHalfPeriod_ = 0;
  uint16_t tonePhase_ = 0;
  int16_t toneSign_ = 1;
};
