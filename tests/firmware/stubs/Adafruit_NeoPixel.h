// Host stub of Adafruit_NeoPixel.
//
// Records the last colour written per pixel so a test can
// assert what the LED layer *would* have shown. It proves
// nothing about a physical strip.

#pragma once

#include <cstdint>
#include <vector>

#define NEO_GRB 0x52
#define NEO_KHZ800 0x0000

namespace hostsim {
extern std::vector<uint32_t> g_pixels;
extern uint8_t g_brightness;
extern int g_showCount;

inline void resetPixels() {
  g_pixels.assign(g_pixels.size(), 0);
  g_showCount = 0;
}
}  // namespace hostsim

class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(uint16_t count, uint8_t, uint8_t) : count_(count) {}

  void begin() { hostsim::g_pixels.assign(count_, 0); }
  void setBrightness(uint8_t value) { hostsim::g_brightness = value; }

  void clear() { hostsim::g_pixels.assign(count_, 0); }

  void setPixelColor(uint16_t index, uint32_t colour) {
    if (index < hostsim::g_pixels.size()) hostsim::g_pixels[index] = colour;
  }

  void fill(uint32_t colour) {
    hostsim::g_pixels.assign(count_, colour);
  }

  void show() { ++hostsim::g_showCount; }

  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
  }

  uint32_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val) {
    return (static_cast<uint32_t>(hue) << 8) | (sat ^ val);
  }

  uint32_t gamma32(uint32_t colour) { return colour; }

 private:
  uint16_t count_;
};
