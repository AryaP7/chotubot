#pragma once
#include <Arduino.h>

// =====================================================
// CENTRAL HARDWARE CONFIGURATION
//
// Every GPIO number in this project lives here and
// nowhere else. Modules include this header instead of
// hard-coding pin numbers.
// =====================================================

namespace Config {

// -----------------------------------------------------
// VERIFIED - physically wired, soldered and tested
// -----------------------------------------------------

constexpr uint8_t PIN_I2C_SDA = 21;   // OLED + DS3231 (+ VL53L0X later)
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_TOUCH   = 4;    // TTP223B OUT, active HIGH

// -----------------------------------------------------
// UNVERIFIED / TO BE WIRED
//
// Chosen to avoid: GPIO6-11 (internal SPI flash),
// GPIO0/2/12/15 (boot strapping pins).
// Nothing below is referenced by Phase 1 code.
// -----------------------------------------------------

// MAX98357A - I2S0 audio out
constexpr uint8_t PIN_AMP_LRC  = 25;
constexpr uint8_t PIN_AMP_BCLK = 26;
constexpr uint8_t PIN_AMP_DIN  = 27;
constexpr uint8_t PIN_AMP_SD   = 19;  // HIGH = enabled, LOW = hard mute

// INMP441 - I2S1 audio in
constexpr uint8_t PIN_MIC_SCK = 14;
constexpr uint8_t PIN_MIC_WS  = 13;
constexpr uint8_t PIN_MIC_SD  = 34;   // input-only pin, ideal for mic data

// WS2812B
constexpr uint8_t PIN_LED_DATA = 18;

// VL53L0X
constexpr uint8_t PIN_TOF_XSHUT = 16;

// Battery sense - ADC1 only (ADC2 dies when WiFi is on)
constexpr uint8_t PIN_BATT_SENSE = 35;

// -----------------------------------------------------
// BUS / TIMING
// -----------------------------------------------------

// 400 kHz. The SH1106 needs 1024 bytes pushed per frame;
// at the Arduino default of 100 kHz that alone costs
// ~92 ms, which is slower than the animation frame
// durations in the Mochi header. Every device on this
// bus (SH1106, DS3231, VL53L0X) is rated for 400 kHz.
constexpr uint32_t I2C_FREQ_HZ = 400000;

constexpr uint32_t SERIAL_BAUD = 115200;

// TTP223B contact debounce. Much shorter than the old
// 400 ms whole-gesture lockout - that value stopped a
// second tap ever being seen, which multi-tap needs.
constexpr uint32_t TOUCH_DEBOUNCE_MS = 35;

// Held at least this long is a long press.
constexpr uint32_t TOUCH_LONG_MS = 800;

// A further tap inside this window extends the gesture
// (1 tap -> short, 2 -> double, 3 -> triple).
constexpr uint32_t TOUCH_MULTI_WINDOW_MS = 320;

// -----------------------------------------------------
// WS2812B
// -----------------------------------------------------

constexpr uint16_t LED_COUNT = 8;

// 255 would let 8 LEDs pull ~480 mA on their own. The
// MT3608 only gives ~1 A total and the amplifier wants a
// share of it, so brightness is capped in firmware
// rather than left to whatever a pattern asks for.
constexpr uint8_t LED_MAX_BRIGHTNESS = 100;   // ~40%

constexpr uint16_t LED_FRAME_INTERVAL_MS = 25;  // 40 Hz

// -----------------------------------------------------
// VL53L0X
//
// Thresholds overlap deliberately - the gap between the
// enter and exit distance is the hysteresis that stops
// the bot flickering between states when someone sits at
// the boundary.
// -----------------------------------------------------

constexpr uint16_t TOF_NEARBY_ENTER_MM = 300;
constexpr uint16_t TOF_NEARBY_EXIT_MM = 450;

constexpr uint16_t TOF_APPROACH_ENTER_MM = 800;
constexpr uint16_t TOF_APPROACH_EXIT_MM = 1000;

// Readings beyond this are treated as "nothing there".
constexpr uint16_t TOF_MAX_VALID_MM = 1800;

// Consecutive agreeing samples before a state is
// accepted. At 50 ms per sample this is ~150 ms.
constexpr uint8_t TOF_CONFIRM_SAMPLES = 3;

constexpr uint16_t TOF_SAMPLE_INTERVAL_MS = 50;

// Hard cap so a wedged sensor can never stall the loop.
constexpr uint16_t TOF_IO_TIMEOUT_MS = 20;

// -----------------------------------------------------
// AUDIO
//
// 16 kHz mono is what speech-to-text backends want, and
// it is a quarter of the bandwidth of 44.1 kHz. There is
// no music being captured here.
// -----------------------------------------------------

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;

// 256 samples = 16 ms per block at 16 kHz. Small enough
// that VAD reacts quickly, large enough that RMS is
// meaningful.
constexpr uint16_t MIC_BLOCK_SAMPLES = 256;

// -----------------------------------------------------
// VOICE ACTIVITY DETECTION
//
// Runs on-device purely to stop us streaming silence to
// the backend. Wake-word recognition is NOT done here -
// see DECISIONS.md D1.
// -----------------------------------------------------

// Speech must exceed the rolling noise floor by this
// factor, in eighths (24 = 3.0x).
constexpr uint16_t VAD_TRIGGER_RATIO_EIGHTHS = 24;

// Release at a lower ratio than it triggers, so a
// wavering voice does not chop the segment up.
constexpr uint16_t VAD_RELEASE_RATIO_EIGHTHS = 17;

// Absolute floor. Below this it is silence regardless of
// what the adaptive floor has drifted to.
constexpr uint16_t VAD_MIN_RMS = 220;

constexpr uint8_t VAD_ATTACK_BLOCKS = 2;    // ~32 ms
constexpr uint8_t VAD_HANGOVER_BLOCKS = 19; // ~300 ms

// -----------------------------------------------------
// AUDIO OUTPUT
// -----------------------------------------------------

constexpr uint16_t AUDIO_TX_CHUNK_SAMPLES = 128;
constexpr uint8_t AUDIO_DEFAULT_VOLUME = 60;   // percent

}  // namespace Config
