#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include "mochi_animations_128x64.h"

// =====================================================
// COMPONENTS
// =====================================================

// OLED Display - 1.3 inch OLED Display (SH1106)
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// RTC Module - DS3231
RTC_DS3231 rtc;

// Touch Sensor - TTP223B
const int TOUCH_PIN = 4;


// =====================================================
// MOCHI
// =====================================================

int currentAnimation = 0;
int currentFrame = 0;

unsigned long lastFrameTime = 0;


// This buffer holds ONE decompressed Mochi frame.
// 128 x 64 / 8 = 1024 bytes
uint8_t mochiFrameBuffer[MOCHI_BYTES_PER_FRAME];


// =====================================================
// MODE
// =====================================================

// false = Mochi
// true  = Clock
bool clockMode = false;

bool lastTouchState = LOW;

unsigned long lastTouchTime = 0;

const unsigned long TOUCH_DEBOUNCE = 400;


// =====================================================
// DRAW MOCHI
// =====================================================

void drawMochiFrame() {

  const MochiAnimation& animation =
      *mochi_animations[currentAnimation];

  // Decode compressed frame into RAM buffer
  bool success = mochiDecodeFrame(
    animation,
    currentFrame,
    mochiFrameBuffer
  );

  if (!success) {
    return;
  }

  oled.clearBuffer();

  // 128 x 64 bitmap
  //
  // 128 pixels / 8 = 16 bytes per row
  oled.drawBitmap(
    0,
    0,
    16,
    64,
    mochiFrameBuffer
  );

  oled.sendBuffer();
}


// =====================================================
// UPDATE MOCHI ANIMATION
// =====================================================

void updateMochi() {

  const MochiAnimation& animation =
      *mochi_animations[currentAnimation];

  // Get frame duration from compressed header
  uint16_t duration =
      mochiFrameDuration(
        animation,
        currentFrame
      );

  if (millis() - lastFrameTime >= duration) {

    lastFrameTime = millis();

    currentFrame++;

    // Finished current animation
    if (currentFrame >= animation.frameCount) {

      currentFrame = 0;

      currentAnimation++;

      // Finished all 18 animations
      if (currentAnimation >= MOCHI_ANIMATION_COUNT) {
        currentAnimation = 0;
      }

      Serial.print("Mochi animation: ");
      Serial.println(
        mochi_animations[currentAnimation]->name
      );
    }

    drawMochiFrame();
  }
}


// =====================================================
// CLOCK
// =====================================================

void drawClock() {

  DateTime now = rtc.now();

  oled.clearBuffer();

  // -------------------------
  // TIME
  // -------------------------

  char timeText[6];

  sprintf(
    timeText,
    "%02d:%02d",
    now.hour(),
    now.minute()
  );

  oled.setFont(u8g2_font_logisoso24_tf);

  oled.drawStr(
    27,
    31,
    timeText
  );


  // -------------------------
  // SECONDS
  // -------------------------

  char secondsText[3];

  sprintf(
    secondsText,
    "%02d",
    now.second()
  );

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(
    58,
    44,
    secondsText
  );


  // -------------------------
  // DATE
  // -------------------------

  char dateText[11];

  sprintf(
    dateText,
    "%02d/%02d/%04d",
    now.day(),
    now.month(),
    now.year()
  );

  oled.drawStr(
    34,
    61,
    dateText
  );

  oled.sendBuffer();
}


// =====================================================
// TOUCH SENSOR
// =====================================================

void checkTouch() {

  bool touchState = digitalRead(TOUCH_PIN);

  // New touch detected
  if (
    touchState == HIGH &&
    lastTouchState == LOW &&
    millis() - lastTouchTime > TOUCH_DEBOUNCE
  ) {

    lastTouchTime = millis();

    clockMode = !clockMode;

    if (clockMode) {

      Serial.println("MODE: CLOCK");

    } else {

      Serial.println("MODE: MOCHI");

      currentFrame = 0;
      lastFrameTime = millis();

      drawMochiFrame();
    }
  }

  lastTouchState = touchState;
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  // Touch Sensor - TTP223B
  pinMode(TOUCH_PIN, INPUT);

  // I2C
  //
  // ESP32:
  // SDA = GPIO 21
  // SCL = GPIO 22
  //
  // OLED Display - SH1106
  // and RTC Module - DS3231
  // share the same I2C bus.
  Wire.begin(21, 22);

  // OLED Display - SH1106
  oled.begin();

  // RTC Module - DS3231
  if (!rtc.begin()) {

    Serial.println("DS3231 NOT FOUND");

    oled.clearBuffer();

    oled.setFont(
      u8g2_font_ncenB08_tr
    );

    oled.drawStr(
      15,
      32,
      "DS3231 ERROR"
    );

    oled.sendBuffer();

    while (true) {
      delay(100);
    }
  }

  Serial.println("BOT READY");

  // Start with HAPPY animation
  for (int i = 0; i < MOCHI_ANIMATION_COUNT; i++) {

    if (
      strcmp(
        mochi_animations[i]->name,
        "happy"
      ) == 0
    ) {

      currentAnimation = i;
      break;
    }
  }

  currentFrame = 0;
  lastFrameTime = millis();

  drawMochiFrame();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  checkTouch();

  if (clockMode) {

    drawClock();

    delay(100);

  } else {

    updateMochi();
  }
}