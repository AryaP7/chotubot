#include "DisplayManager.h"
#include <Wire.h>
#include "../Config.h"

bool DisplayManager::begin() {
  ready_ = oled_.begin();

  // u8g2's begin() sets its own bus speed; force ours
  // afterwards so the whole I2C bus runs at 400 kHz.
  Wire.setClock(Config::I2C_FREQ_HZ);

  return ready_;
}

void DisplayManager::showMessage(const char* line) {
  if (!ready_) return;

  oled_.clearBuffer();
  oled_.setFont(u8g2_font_ncenB08_tr);

  const int16_t w = oled_.getStrWidth(line);
  oled_.drawStr((128 - w) / 2, 36, line);

  oled_.sendBuffer();
}
