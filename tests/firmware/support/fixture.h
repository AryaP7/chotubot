// Reset every stub between tests.
//
// Without this the fake clock, pin states and queued
// samples leak from one test into the next and failures
// depend on ordering.

#pragma once

#include "Arduino.h"
#include "Adafruit_NeoPixel.h"
#include "ESP_I2S.h"
#include "NetworkClient.h"
#include "VL53L0X.h"
#include "esp_random.h"

namespace hostsim {

inline void resetAll() {
  resetClock();
  resetPins();
  clearSerial();
  resetTof();
  resetMic();
  resetNet();
  resetPixels();
  seedRandom(0x12345678);
}

}  // namespace hostsim
