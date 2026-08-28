#include "NotificationScreen.h"

#include "../net/BackendClient.h"
#include "DisplayManager.h"

namespace {

constexpr uint32_t kVisibleMs = 6000;
constexpr uint32_t kFrameIntervalMs = 200;
constexpr int16_t kScreenWidth = 128;

// Break text at the last space that still fits.
uint16_t fitChars(U8G2& g, const char* text, uint16_t from, int16_t maxWidth) {
  const uint16_t len = strlen(text);
  uint16_t best = from;
  char buf[42];

  for (uint16_t end = from + 1; end <= len && (end - from) < sizeof(buf);
       ++end) {
    const uint16_t n = end - from;
    memcpy(buf, text + from, n);
    buf[n] = '\0';

    if (g.getStrWidth(buf) > maxWidth) break;

    if (text[end] == ' ' || text[end] == '\0') best = end;
  }

  return (best > from) ? best : len;
}

}  // namespace

void NotificationScreen::show(uint32_t nowMs) {
  shownAtMs_ = nowMs;
  lastRenderMs_ = 0;
}

bool NotificationScreen::isExpired(uint32_t nowMs) const {
  return (nowMs - shownAtMs_) > kVisibleMs;
}

bool NotificationScreen::update(DisplayManager& display,
                                const Notification& notification,
                                uint32_t nowMs) {
  if (!display.isReady()) return false;
  if (lastRenderMs_ != 0 && (nowMs - lastRenderMs_) < kFrameIntervalMs) {
    return false;
  }
  lastRenderMs_ = nowMs;

  U8G2& g = display.gfx();
  g.clearBuffer();

  // Border, so it reads as an interruption rather than a
  // normal screen.
  g.drawFrame(0, 0, 128, 64);

  g.setFont(u8g2_font_6x12_tf);
  const char* title = notification.valid ? notification.title : "Notification";
  g.drawStr((kScreenWidth - g.getStrWidth(title)) / 2, 16, title);
  g.drawHLine(4, 20, 120);

  // Body across up to three wrapped lines.
  g.setFont(u8g2_font_5x7_tf);
  const char* body = notification.valid ? notification.body : "";

  uint16_t pos = 0;
  const uint16_t len = strlen(body);
  int16_t y = 33;
  char line[42];

  for (uint8_t row = 0; row < 3 && pos < len; ++row) {
    const uint16_t end = fitChars(g, body, pos, 116);
    uint16_t n = end - pos;
    if (n >= sizeof(line)) n = sizeof(line) - 1;

    memcpy(line, body + pos, n);
    line[n] = '\0';

    g.drawStr(6, y, line);
    y += 10;

    pos = end;
    while (pos < len && body[pos] == ' ') pos++;
  }

  // Countdown bar so the dismissal is not a surprise.
  const uint32_t elapsed = nowMs - shownAtMs_;
  if (elapsed < kVisibleMs) {
    const uint16_t w = static_cast<uint16_t>(
        (static_cast<uint64_t>(kVisibleMs - elapsed) * 120) / kVisibleMs);
    g.drawBox(4, 59, w, 2);
  }

  g.sendBuffer();
  return true;
}
