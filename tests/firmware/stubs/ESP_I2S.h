// Host stub of the ESP32 core's I2S driver.
//
// Feeds the microphone whatever 32-bit samples a test
// queues. The real Microphone::readBlock() -- shift, RMS,
// adaptive noise floor -- and the real VAD hysteresis run
// unmodified against them.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace hostsim {

extern std::vector<int32_t> g_micSamples;
extern size_t g_micCursor;
extern bool g_micBegin;

// Queue raw 32-bit I2S frames. The INMP441 presents 24-bit
// data left-justified in 32 bits, so a 16-bit amplitude is
// written as (value << 16).
inline void queueMicSamples(const std::vector<int32_t>& samples) {
  g_micSamples.insert(g_micSamples.end(), samples.begin(), samples.end());
}

inline void queueMicTone(size_t count, int16_t amplitude) {
  std::vector<int32_t> block;
  block.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const int16_t value = (i % 2 == 0) ? amplitude
                                       : static_cast<int16_t>(-amplitude);
    block.push_back(static_cast<int32_t>(value) << 16);
  }
  queueMicSamples(block);
}

inline void queueMicSilence(size_t count) { queueMicTone(count, 0); }

inline void resetMic() {
  g_micSamples.clear();
  g_micCursor = 0;
  g_micBegin = true;
}

inline void setMicBegin(bool ok) { g_micBegin = ok; }

}  // namespace hostsim

typedef int i2s_port_t;
#define I2S_NUM_0 0
#define I2S_NUM_1 1

enum i2s_mode_t { I2S_MODE_STD };
enum i2s_data_bit_width_t {
  I2S_DATA_BIT_WIDTH_16BIT = 16,
  I2S_DATA_BIT_WIDTH_32BIT = 32
};
enum i2s_slot_mode_t { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 };
#define I2S_STD_SLOT_LEFT 0

class I2SClass {
 public:
  explicit I2SClass(i2s_port_t = 0) {}

  void setPins(int8_t, int8_t, int8_t, int8_t = -1, int8_t = -1) {}

  bool begin(i2s_mode_t, uint32_t, i2s_data_bit_width_t, i2s_slot_mode_t,
             int8_t = -1) {
    return hostsim::g_micBegin;
  }

  int available() {
    const size_t remaining = hostsim::g_micSamples.size() - hostsim::g_micCursor;
    return static_cast<int>(remaining * sizeof(int32_t));
  }

  size_t readBytes(char* buffer, size_t size) {
    const size_t wantSamples = size / sizeof(int32_t);
    const size_t have = hostsim::g_micSamples.size() - hostsim::g_micCursor;
    const size_t take = (wantSamples < have) ? wantSamples : have;

    memcpy(buffer, hostsim::g_micSamples.data() + hostsim::g_micCursor,
           take * sizeof(int32_t));
    hostsim::g_micCursor += take;
    return take * sizeof(int32_t);
  }

  size_t write(const uint8_t*, size_t size) { return size; }
  size_t write(const void*, size_t size) { return size; }
};
