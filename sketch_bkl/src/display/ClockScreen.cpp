#include "ClockScreen.h"
#include "DisplayManager.h"
#include "../rtc/RtcManager.h"

bool ClockScreen::update(DisplayManager& display, RtcManager& rtc) {
  const DateTime now = rtc.now();

  // Nothing visible changes within a second, so skip the
  // 1 KB I2C transfer entirely.
  if (now.second() == lastSecond_) {
    return false;
  }
  lastSecond_ = now.second();

  U8G2& g = display.gfx();
  g.clearBuffer();

  char timeText[6];
  snprintf(timeText, sizeof(timeText), "%02d:%02d", now.hour(), now.minute());
  g.setFont(u8g2_font_logisoso24_tf);
  g.drawStr(27, 31, timeText);

  char secondsText[3];
  snprintf(secondsText, sizeof(secondsText), "%02d", now.second());
  g.setFont(u8g2_font_6x12_tf);
  g.drawStr(58, 44, secondsText);

  char dateText[11];
  snprintf(dateText, sizeof(dateText), "%02d/%02d/%04d",
           now.day(), now.month(), now.year());
  g.drawStr(34, 61, dateText);

  g.sendBuffer();
  return true;
}
