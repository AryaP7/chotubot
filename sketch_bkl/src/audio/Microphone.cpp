#include "Microphone.h"

#include <ESP_I2S.h>

#include "../Config.h"
#include "../core/Events.h"

// I2S1 explicitly. The amplifier takes I2S0, and letting
// both auto-allocate risks them landing on the same
// peripheral.
static I2SClass i2sMic(I2S_NUM_1);

// One block of raw 32-bit frames, and the 16-bit mono
// conversion handed to callers. 1 KB + 512 B static.
static int32_t rawBlock[Config::MIC_BLOCK_SAMPLES];
static int16_t pcmBlock[Config::MIC_BLOCK_SAMPLES];

bool Microphone::begin() {
  // dout = -1: this instance only ever receives.
  i2sMic.setPins(Config::PIN_MIC_SCK, Config::PIN_MIC_WS, -1,
                 Config::PIN_MIC_SD);

  // INMP441 presents 24-bit data left-justified in a
  // 32-bit slot, so we take 32 bits and shift. L/R is
  // tied to GND on the module, which is the LEFT slot.
  ready_ = i2sMic.begin(I2S_MODE_STD, Config::AUDIO_SAMPLE_RATE,
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO,
                        I2S_STD_SLOT_LEFT);

  if (!ready_) {
    Serial.println(F("[mic] INMP441 I2S init failed"));
    return false;
  }

  Serial.println(F("[mic] INMP441 ready"));
  return true;
}

bool Microphone::readBlock() {
  const size_t wanted = sizeof(rawBlock);

  // Nothing buffered yet - do not block waiting for it.
  if (i2sMic.available() <= 0) return false;

  const size_t got = i2sMic.readBytes(reinterpret_cast<char*>(rawBlock), wanted);
  if (got < sizeof(int32_t)) return false;

  const uint16_t samples = static_cast<uint16_t>(got / sizeof(int32_t));
  validSamples_ = samples;

  // Sum of squares in 64-bit: 256 samples of a full
  // scale 16-bit value overflows 32 bits.
  uint64_t sumSquares = 0;

  for (uint16_t i = 0; i < samples; ++i) {
    // Top 16 bits of the 24-bit sample.
    const int16_t s = static_cast<int16_t>(rawBlock[i] >> 16);
    pcmBlock[i] = s;
    sumSquares += static_cast<uint64_t>(static_cast<int32_t>(s) * s);
  }

  const uint32_t meanSquare = static_cast<uint32_t>(sumSquares / samples);
  lastRms_ = static_cast<uint16_t>(sqrt(static_cast<double>(meanSquare)));
  return true;
}

void Microphone::update(uint32_t nowMs, EventBus& bus) {
  (void)nowMs;
  if (!ready_) return;
  if (!readBlock()) return;

  // Thresholds in eighths to keep this integer-only.
  const uint32_t floor32 = noiseFloor_ ? noiseFloor_ : 1;
  uint32_t trigger = (floor32 * Config::VAD_TRIGGER_RATIO_EIGHTHS) / 8;
  uint32_t release = (floor32 * Config::VAD_RELEASE_RATIO_EIGHTHS) / 8;

  if (trigger < Config::VAD_MIN_RMS) trigger = Config::VAD_MIN_RMS;
  if (release < Config::VAD_MIN_RMS / 2) release = Config::VAD_MIN_RMS / 2;

  if (!voiceActive_) {
    if (lastRms_ > trigger) {
      if (++attackCount_ >= Config::VAD_ATTACK_BLOCKS) {
        voiceActive_ = true;
        attackCount_ = 0;
        hangoverCount_ = Config::VAD_HANGOVER_BLOCKS;
        bus.publish(EventType::VoiceStarted, lastRms_);
      }
    } else {
      attackCount_ = 0;

      // Adapt only while quiet, so a long sentence cannot
      // drag the floor up and deafen the gate.
      noiseFloor_ = static_cast<uint16_t>(
          ((static_cast<uint32_t>(noiseFloor_) * 15) + lastRms_) / 16);
    }
    return;
  }

  // Voiced: hold on until it has been quiet for the whole
  // hangover window.
  if (lastRms_ > release) {
    hangoverCount_ = Config::VAD_HANGOVER_BLOCKS;
    return;
  }

  if (hangoverCount_ > 0) {
    hangoverCount_--;
    return;
  }

  voiceActive_ = false;
  bus.publish(EventType::VoiceStopped, lastRms_);
}

uint16_t Microphone::readAudio(const int16_t*& outSamples) const {
  if (!ready_ || validSamples_ == 0) {
    outSamples = nullptr;
    return 0;
  }
  outSamples = pcmBlock;
  return validSamples_;
}
