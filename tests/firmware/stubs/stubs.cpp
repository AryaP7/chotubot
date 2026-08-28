// Storage for the host stubs' mutable state.

#include "Arduino.h"

#include "Adafruit_NeoPixel.h"
#include "ESP_I2S.h"
#include "NetworkClient.h"
#include "VL53L0X.h"
#include "esp_random.h"

namespace hostsim {

uint32_t g_millis = 0;
int g_pinState[64] = {0};
int g_pinMode[64] = {0};
std::string g_serialLog;
uint32_t g_adcMilliVolts = 0;

}  // namespace hostsim

HostSerial Serial;
HostEsp ESP;

// --- VL53L0X fake sensor state ---
namespace hostsim {
bool g_tofPresent = true;
uint16_t g_tofDistance = 8190;
bool g_tofTimeout = false;
}  // namespace hostsim

// --- I2S fake microphone state ---
namespace hostsim {
std::vector<int32_t> g_micSamples;
size_t g_micCursor = 0;
bool g_micBegin = true;
}  // namespace hostsim

// --- NetworkClient fake socket state ---
namespace hostsim {
std::string g_netRxBuffer;
std::string g_netTxBuffer;
size_t g_netRxCursor = 0;
bool g_netConnectOk = true;
bool g_netConnected = false;
}  // namespace hostsim

// --- WS2812B fake strip state ---
namespace hostsim {
std::vector<uint32_t> g_pixels;
uint8_t g_brightness = 0;
int g_showCount = 0;
}  // namespace hostsim

// --- deterministic randomness ---
namespace hostsim {
uint32_t g_randomSeed = 0x12345678;
}  // namespace hostsim
