// =====================================================
// CHOTUBOT - desktop companion
//
// PHASE 2: event bus + state machine + diagnostics,
// layered onto the Phase 1 refactor.
//
// The verified V1 interaction is unchanged:
//   Mochi animation -> touch -> clock -> touch -> Mochi
//   and Idle still cycles all 18 animations.
//
// VERIFIED HARDWARE (soldered and tested):
//   ESP32-WROOM-32
//   OLED SH1106 128x64   I2C  GPIO21 / GPIO22
//   RTC DS3231           I2C  GPIO21 / GPIO22
//   Touch TTP223B             GPIO4
//
// Wake-word detection is a BACKEND concern and is
// deliberately absent here. See DECISIONS.md D1.
//
// SERIAL COMMANDS
//   d  toggle the diagnostics screen
//   s  print system status
//   t  set the DS3231 from this sketch's compile time
// =====================================================

#include <Wire.h>

#include "src/Config.h"
#include "src/audio/AudioOutput.h"
#include "src/audio/Microphone.h"
#include "src/core/Events.h"
#include "src/core/StateMachine.h"
#include "src/diag/Diagnostics.h"
#include "src/display/ClockScreen.h"
#include "src/display/DisplayManager.h"
#include "src/display/MochiPlayer.h"
#include "src/display/NotificationScreen.h"
#include "src/display/NowPlayingScreen.h"
#include "src/input/TouchInput.h"
#include "src/lighting/LedController.h"
#include "src/net/BackendClient.h"
#include "src/net/WifiManager.h"
#include "src/power/BatteryMonitor.h"
#include "src/rtc/RtcManager.h"
#include "src/rtc/Scheduler.h"
#include "src/sensors/ProximitySensor.h"

// -----------------------------------------------------
// MODULES
// -----------------------------------------------------

static DisplayManager display;
static MochiPlayer mochi;
static ClockScreen clockScreen;
static TouchInput touch;
static RtcManager rtc;
static ProximitySensor proximity;
static LedController leds;
static Microphone microphone;
static AudioOutput audio;
static WifiManager wifi;
static BackendClient backend;
static Scheduler scheduler;
static BatteryMonitor battery;
static NowPlayingScreen nowPlayingScreen;
static NotificationScreen notificationScreen;

static EventBus bus;
static StateMachine machine;
static Diagnostics diag;

// -----------------------------------------------------

static void drawMochi() {
  // Straight into u8g2's buffer. No clearBuffer needed -
  // the blit writes all 1024 bytes.
  mochi.blitTo(display.buffer());
  display.gfx().sendBuffer();
}

static void handleSerialCommand(char c, uint32_t nowMs) {
  switch (c) {
    case 'd':
      machine.transitionTo(machine.state() == BotState::Diagnostics
                               ? BotState::Idle
                               : BotState::Diagnostics,
                           nowMs);
      break;

    case 's':
      diag.printSerial();
      break;

    case 'p':
      scheduler.printSerial();
      Serial.print(F("battery\t"));
      if (battery.hasPlausibleReading()) {
        Serial.print(battery.millivolts());
        Serial.println(F(" mV"));
      } else {
        Serial.println(F("not connected"));
      }
      break;

    case 'a':
      // Stage 08 check: proves the amplifier and speaker
      // are alive. 440 Hz for half a second.
      audio.playTone(440, 500);
      break;

    case 'k':
      // Round-trip check against the backend.
      Serial.println(F("[backend] ping ->"));
      backend.sendPing();
      break;

    case 'g':
      proximity.setGesturesEnabled(!proximity.gesturesEnabled());
      Serial.print(F("[gesture] detection "));
      Serial.println(proximity.gesturesEnabled() ? "ON (untuned)" : "off");
      break;

    case 'm':
      // Stage 09 check: RMS against the adaptive noise
      // floor. Speak and the first number should jump.
      Serial.print(F("[mic] rms="));
      Serial.print(microphone.level());
      Serial.print(F(" floor="));
      Serial.print(microphone.noiseFloor());
      Serial.print(F(" voice="));
      Serial.println(microphone.isVoiceDetected() ? "YES" : "no");
      break;

    case 't':
      // Deliberately manual. The RTC is never written on
      // boot, so reflashing does not silently rewrite the
      // time. Note this sets COMPILE time, so it will be
      // a little behind by the time you type it.
      rtc.setDateTime(DateTime(F(__DATE__), F(__TIME__)));
      break;

    default:
      break;
  }
}

// -----------------------------------------------------

void setup() {
  Serial.begin(Config::SERIAL_BAUD);

  touch.begin();
  diag.set(Subsystem::Touch, Health::Ok);  // pin reads; no handshake exists

  Wire.begin(Config::PIN_I2C_SDA, Config::PIN_I2C_SCL);
  Wire.setClock(Config::I2C_FREQ_HZ);

  if (display.begin()) {
    diag.set(Subsystem::Oled, Health::Ok);
  } else {
    diag.set(Subsystem::Oled, Health::Failed);
    Serial.println(F("[display] SH1106 init failed"));
  }

  // A missing RTC no longer halts the bot. Mochi and
  // touch stay alive; only the clock screen degrades.
  if (rtc.begin()) {
    diag.set(Subsystem::Rtc, Health::Ok);
  } else {
    diag.set(Subsystem::Rtc, Health::Failed);
    display.showMessage("DS3231 ERROR");
    delay(1500);
  }

  // VL53L0X shares the I2C bus that is already up.
  if (proximity.begin()) {
    diag.set(Subsystem::Proximity, Health::Ok);
  } else {
    diag.set(Subsystem::Proximity, Health::Failed);
  }

  // A WS2812B strip has no readback line, so there is no
  // honest way to confirm it is attached. Health stays
  // NotTested even though the driver started. D6.
  leds.begin();

  // I2S1 - microphone. Init failing here means the bus
  // could not be configured; it does not prove an
  // INMP441 is actually on the end of it.
  diag.set(Subsystem::Microphone,
           microphone.begin() ? Health::Ok : Health::Failed);

  // I2S0 - amplifier. Same caveat.
  diag.set(Subsystem::Audio, audio.begin() ? Health::Ok : Health::Failed);

  scheduler.begin();
  battery.begin();

  // Network last. Everything above works without it.
  wifi.begin();
  backend.begin(audio);

  mochi.begin();
  if (!mochi.selectByName("happy")) {
    mochi.selectIndex(0);
  }

  const uint32_t now = millis();
  machine.begin(mochi, leds, now);
  drawMochi();

  Serial.print(F("[boot] ready - "));
  Serial.print(mochi.animationCount());
  Serial.println(F(" animations"));
  diag.printSerial();
}

void loop() {
  const uint32_t now = millis();

  // ---- Inputs publish, they never act ----
  switch (touch.update(now)) {
    case TouchEvent::Short:  bus.publish(EventType::TouchShort);  break;
    case TouchEvent::Double: bus.publish(EventType::TouchDouble); break;
    case TouchEvent::Long:   bus.publish(EventType::TouchLong);   break;
    case TouchEvent::Triple: bus.publish(EventType::TouchTriple); break;
    case TouchEvent::None:   break;
  }

  if (Serial.available() > 0) {
    handleSerialCommand(static_cast<char>(Serial.read()), now);
  }

  proximity.update(now, bus);
  microphone.update(now, bus);
  scheduler.update(now, rtc, bus);
  battery.update(now, bus);

  wifi.update(now, bus);
  if (wifi.isConnected()) {
    backend.update(now, bus);
  }

  // Voice uplink. Audio only leaves the device while the
  // local VAD says someone is talking - see DECISIONS.md
  // D1. The backend decides if it was the wake word.
  if (microphone.isVoiceDetected() && backend.isConnected()) {
    const int16_t* samples = nullptr;
    const uint16_t count = microphone.readAudio(samples);
    if (count > 0) {
      backend.sendAudio(samples, count);
    }
  }

  // ---- The state machine is the only thing that acts ----
  Event event;
  while (bus.poll(event)) {
    // Network side-effects. Kept out of the state machine
    // so it stays about behaviour, not transport.
    switch (event.type) {
      case EventType::WifiConnected:
        backend.onNetworkUp();
        break;

      case EventType::WifiDisconnected:
        backend.onNetworkDown();
        diag.set(Subsystem::Backend, Health::NotTested);
        break;

      case EventType::VoiceStarted:
        backend.beginVoiceSegment();
        break;

      case EventType::VoiceStopped:
        backend.endVoiceSegment();
        break;

      case EventType::TouchShort:
        backend.sendEvent("TOUCH_SHORT");
        break;

      case EventType::TouchDouble:
        backend.sendEvent("TOUCH_DOUBLE");
        break;

      case EventType::TouchLong:
        backend.sendEvent("TOUCH_LONG");
        break;

      case EventType::TouchTriple:
        backend.sendEvent("TOUCH_TRIPLE");
        break;

      case EventType::NotificationReceived:
      case EventType::AlarmFired:
        // Audible cue, then the screen takes over.
        notificationScreen.show(now);
        if (!scheduler.isQuiet()) {
          audio.playTone(880, 140);
        }
        break;

      case EventType::GestureApproach:
      case EventType::GestureRetreat:
      case EventType::GestureHover:
        // Reported only. Nothing acts on these until the
        // thresholds are proven on real hardware.
        Serial.print(F("[gesture] "));
        Serial.print(eventName(event.type));
        Serial.print(' ');
        Serial.println(event.data);
        break;

      default:
        break;
    }

    if (machine.handle(event, now)) {
      // A transition may have changed what is on screen.
      switch (machine.state()) {
        case BotState::Clock:       clockScreen.invalidate();      break;
        case BotState::NowPlaying:  nowPlayingScreen.invalidate(); break;
        case BotState::Diagnostics: break;
        case BotState::Notification: break;
        default:                    drawMochi();                   break;
      }
    }
  }

  // ---- Actuators ----
  audio.update(now, bus);
  leds.update(now);

  // Health that can only be known from live state.
  diag.set(Subsystem::Wifi,
           wifi.isConnected() ? Health::Ok
                              : (wifi.state() == WifiState::Disabled
                                     ? Health::NotTested
                                     : Health::Failed));
  if (backend.isConnected()) {
    diag.set(Subsystem::Backend, Health::Ok);
  }

  diag.setBattery(battery.millivolts(), battery.percentEstimate(),
                  battery.hasPlausibleReading());

  // ---- Render the current state ----
  switch (machine.state()) {
    case BotState::Clock:
      clockScreen.update(display, rtc);
      break;

    case BotState::Diagnostics:
      diag.render(display, now);
      break;

    case BotState::NowPlaying:
      nowPlayingScreen.update(display, backend.spotify(), now);
      break;

    case BotState::Notification:
      notificationScreen.update(display, backend.notification(), now);
      // Hand the screen back once it has had its moment.
      if (notificationScreen.isExpired(now)) {
        machine.transitionTo(BotState::Idle, now);
        drawMochi();
      }
      break;

    default:
      // Every Mochi-rendering state. Only pushes to the
      // panel when a frame actually changed.
      if (mochi.update(now)) {
        drawMochi();
      }
      break;
  }
}
