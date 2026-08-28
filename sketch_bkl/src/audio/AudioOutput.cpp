#include "AudioOutput.h"

#include <ESP_I2S.h>

#include "../Config.h"
#include "../core/Events.h"

// I2S0 explicitly; the microphone holds I2S1.
static I2SClass i2sAmp(I2S_NUM_0);

// One TX chunk, interleaved L/R.
static int16_t txBuffer[Config::AUDIO_TX_CHUNK_SAMPLES * 2];

bool AudioOutput::begin() {
  pinMode(Config::PIN_AMP_SD, OUTPUT);
  setAmplifierEnabled(false);

  // din = -1: this instance only ever transmits.
  i2sAmp.setPins(Config::PIN_AMP_BCLK, Config::PIN_AMP_LRC,
                 Config::PIN_AMP_DIN, -1);

  ready_ = i2sAmp.begin(I2S_MODE_STD, Config::AUDIO_SAMPLE_RATE,
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

  if (!ready_) {
    Serial.println(F("[audio] MAX98357A I2S init failed"));
    return false;
  }

  volume_ = Config::AUDIO_DEFAULT_VOLUME;
  Serial.println(F("[audio] MAX98357A ready"));
  return true;
}

void AudioOutput::setAmplifierEnabled(bool enabled) {
  // HIGH is well above the 1.4 V threshold, which selects
  // the left channel and powers the amp up.
  digitalWrite(Config::PIN_AMP_SD, enabled ? HIGH : LOW);
}

void AudioOutput::setVolume(uint8_t percent) {
  volume_ = (percent > 100) ? 100 : percent;
}

void AudioOutput::playTone(uint16_t frequencyHz, uint16_t durationMs) {
  if (!ready_ || frequencyHz == 0) return;

  toneHalfPeriod_ =
      static_cast<uint16_t>(Config::AUDIO_SAMPLE_RATE / (frequencyHz * 2));
  if (toneHalfPeriod_ == 0) toneHalfPeriod_ = 1;

  toneSamplesLeft_ =
      (static_cast<uint32_t>(Config::AUDIO_SAMPLE_RATE) * durationMs) / 1000UL;
  tonePhase_ = 0;
  toneSign_ = 1;

  setAmplifierEnabled(true);
  playing_ = true;
}

size_t AudioOutput::writePcm(const int16_t* samples, size_t count) {
  if (!ready_ || samples == nullptr || count == 0) return 0;

  setAmplifierEnabled(true);
  playing_ = true;

  size_t written = 0;

  while (written < count) {
    const size_t chunk =
        min(static_cast<size_t>(Config::AUDIO_TX_CHUNK_SAMPLES),
            count - written);

    for (size_t i = 0; i < chunk; ++i) {
      const int32_t scaled =
          (static_cast<int32_t>(samples[written + i]) * volume_) / 100;
      // MAX98357A is mono but the stream is stereo, so
      // the same sample goes to both slots.
      txBuffer[i * 2] = static_cast<int16_t>(scaled);
      txBuffer[i * 2 + 1] = static_cast<int16_t>(scaled);
    }

    i2sAmp.write(reinterpret_cast<const uint8_t*>(txBuffer),
                 chunk * 2 * sizeof(int16_t));
    written += chunk;
  }

  return written;
}

void AudioOutput::emitToneChunk() {
  const uint32_t chunk =
      min(static_cast<uint32_t>(Config::AUDIO_TX_CHUNK_SAMPLES),
          toneSamplesLeft_);

  // Amplitude is deliberately well below full scale. A
  // square wave at full scale into a small speaker is
  // unpleasant and pulls a current spike the MT3608 will
  // not enjoy.
  const int16_t amplitude = static_cast<int16_t>((6000 * volume_) / 100);

  for (uint32_t i = 0; i < chunk; ++i) {
    if (tonePhase_++ >= toneHalfPeriod_) {
      tonePhase_ = 0;
      toneSign_ = static_cast<int16_t>(-toneSign_);
    }
    const int16_t sample = static_cast<int16_t>(amplitude * toneSign_);
    txBuffer[i * 2] = sample;
    txBuffer[i * 2 + 1] = sample;
  }

  i2sAmp.write(reinterpret_cast<const uint8_t*>(txBuffer),
               chunk * 2 * sizeof(int16_t));
  toneSamplesLeft_ -= chunk;
}

void AudioOutput::update(uint32_t nowMs, EventBus& bus) {
  (void)nowMs;
  if (!ready_ || !playing_) return;

  if (toneSamplesLeft_ > 0) {
    emitToneChunk();
    return;
  }

  // Nothing left to send.
  stop();
  bus.publish(EventType::AudioFinished, 0);
}

void AudioOutput::stop() {
  toneSamplesLeft_ = 0;
  playing_ = false;
  setAmplifierEnabled(false);
}
