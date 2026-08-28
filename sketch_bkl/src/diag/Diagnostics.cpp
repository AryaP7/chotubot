#include "Diagnostics.h"
#include "../display/DisplayManager.h"

const char* subsystemName(Subsystem sub) {
  switch (sub) {
    case Subsystem::Oled:       return "OLED";
    case Subsystem::Rtc:        return "RTC";
    case Subsystem::Touch:      return "TOUCH";
    case Subsystem::Wifi:       return "WIFI";
    case Subsystem::Microphone: return "MIC";
    case Subsystem::Audio:      return "AUDIO";
    case Subsystem::Proximity:  return "TOF";
    case Subsystem::Lighting:   return "LED";
    case Subsystem::Backend:    return "AI";
    case Subsystem::COUNT:      break;
  }
  return "?";
}

const char* healthName(Health health) {
  switch (health) {
    case Health::Ok:        return "OK";
    case Health::Failed:    return "FAIL";
    case Health::NotTested: return "--";
  }
  return "?";
}

uint32_t Diagnostics::freeHeap() const {
  return ESP.getFreeHeap();
}

uint32_t Diagnostics::sketchBytes() const {
  return ESP.getSketchSize();
}

uint32_t Diagnostics::sketchCapacityBytes() const {
  return ESP.getSketchSize() + ESP.getFreeSketchSpace();
}

void Diagnostics::printSerial() const {
  Serial.println(F("--- SYSTEM STATUS ---"));
  for (uint8_t i = 0; i < static_cast<uint8_t>(Subsystem::COUNT); ++i) {
    const Subsystem sub = static_cast<Subsystem>(i);
    Serial.print(subsystemName(sub));
    Serial.print('\t');
    Serial.println(healthName(get(sub)));
  }
  Serial.print(F("heap\t"));
  Serial.println(freeHeap());
  Serial.print(F("flash\t"));
  Serial.print(sketchBytes());
  Serial.print('/');
  Serial.println(sketchCapacityBytes());
  Serial.print(F("uptime\t"));
  Serial.println(uptimeSeconds());
}

void Diagnostics::render(DisplayManager& display, uint32_t nowMs) {
  if (!display.isReady()) return;

  if (rendered_ && (nowMs - lastRenderMs_) < 500UL) return;
  lastRenderMs_ = nowMs;
  rendered_ = true;

  U8G2& g = display.gfx();
  g.clearBuffer();
  g.setFont(u8g2_font_5x7_tf);

  g.drawStr(0, 6, "SYSTEM STATUS");
  g.drawHLine(0, 8, 128);

  // Nine subsystems in two columns of five, 7 px rows.
  const uint8_t count = static_cast<uint8_t>(Subsystem::COUNT);
  for (uint8_t i = 0; i < count; ++i) {
    const Subsystem sub = static_cast<Subsystem>(i);
    const uint8_t col = i / 5;
    const uint8_t row = i % 5;

    const uint8_t x = col ? 68 : 0;
    const uint8_t y = static_cast<uint8_t>(18 + row * 8);

    g.drawStr(x, y, subsystemName(sub));
    g.drawStr(static_cast<uint8_t>(x + 40), y, healthName(get(sub)));
  }

  // Battery line. The ~ is deliberate: the percentage is
  // interpolated from voltage, not measured.
  char batt[26];
  if (battValid_) {
    snprintf(batt, sizeof(batt), "BATT ~%u%%  %u.%02uV", battPct_,
             battMv_ / 1000, (battMv_ % 1000) / 10);
  } else {
    snprintf(batt, sizeof(batt), "BATT --");
  }
  g.drawStr(0, 54, batt);

  char footer[40];
  const uint32_t capacity = sketchCapacityBytes();
  const uint32_t pct = capacity ? (sketchBytes() * 100UL) / capacity : 0;

  snprintf(footer, sizeof(footer), "ram %luk fl %lu%% up %lus",
           static_cast<unsigned long>(freeHeap() / 1024UL),
           static_cast<unsigned long>(pct),
           static_cast<unsigned long>(uptimeSeconds()));
  g.drawStr(0, 62, footer);

  g.sendBuffer();
}
