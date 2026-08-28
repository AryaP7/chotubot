#include "NowPlayingScreen.h"

#include "../net/BackendClient.h"
#include "DisplayManager.h"

namespace {

constexpr uint32_t kFrameIntervalMs = 120;
constexpr uint32_t kScrollIntervalMs = 220;
constexpr int16_t kScreenWidth = 128;

// Draw text at x, scrolling it left when it overflows.
void drawScrolling(U8G2& g, const char* text, int16_t y, uint16_t offset) {
  const int16_t w = g.getStrWidth(text);

  if (w <= kScreenWidth - 4) {
    g.drawStr(2, y, text);
    return;
  }

  // Wrap with a gap so the loop reads as a loop.
  const int16_t span = w + 16;
  const int16_t x = 2 - static_cast<int16_t>(offset % span);
  g.drawStr(x, y, text);
  g.drawStr(x + span, y, text);
}

void formatTime(uint32_t ms, char* out, size_t size) {
  const uint32_t totalSec = ms / 1000UL;
  snprintf(out, size, "%lu:%02lu", static_cast<unsigned long>(totalSec / 60),
           static_cast<unsigned long>(totalSec % 60));
}

}  // namespace

bool NowPlayingScreen::update(DisplayManager& display, const SpotifyState& state,
                              uint32_t nowMs) {
  if (!display.isReady()) return false;
  if (lastRenderMs_ != 0 && (nowMs - lastRenderMs_) < kFrameIntervalMs) {
    return false;
  }
  lastRenderMs_ = nowMs;

  if (nowMs - lastScrollMs_ >= kScrollIntervalMs) {
    lastScrollMs_ = nowMs;
    scrollOffset_ += 4;
  }

  U8G2& g = display.gfx();
  g.clearBuffer();

  g.setFont(u8g2_font_5x7_tf);

  if (!state.valid || state.status == SpotifyStatus::Idle) {
    g.drawStr(28, 30, "SPOTIFY IDLE");
    g.sendBuffer();
    return true;
  }

  if (state.status == SpotifyStatus::Error) {
    g.drawStr(22, 30, "SPOTIFY ERROR");
    g.sendBuffer();
    return true;
  }

  // Header
  const char* header =
      (state.status == SpotifyStatus::Paused) ? "PAUSED" : "NOW PLAYING";
  g.drawStr((kScreenWidth - g.getStrWidth(header)) / 2, 8, header);
  g.drawHLine(0, 11, kScreenWidth);

  // Track, in the larger face since it matters most.
  g.setFont(u8g2_font_7x13_tf);
  drawScrolling(g, state.track, 27, scrollOffset_);

  // Artist
  g.setFont(u8g2_font_5x7_tf);
  drawScrolling(g, state.artist, 41, scrollOffset_ / 2);

  // Progress
  char elapsed[8];
  char total[8];
  formatTime(state.progressMs, elapsed, sizeof(elapsed));
  formatTime(state.durationMs, total, sizeof(total));

  g.drawStr(2, 62, elapsed);
  g.drawStr(kScreenWidth - g.getStrWidth(total) - 2, 62, total);

  const int16_t barX = 34;
  const int16_t barW = 60;
  g.drawFrame(barX, 56, barW, 5);

  if (state.durationMs > 0) {
    uint32_t filled =
        (static_cast<uint64_t>(state.progressMs) * (barW - 2)) /
        state.durationMs;
    if (filled > static_cast<uint32_t>(barW - 2)) filled = barW - 2;
    if (filled > 0) {
      g.drawBox(barX + 1, 57, static_cast<uint16_t>(filled), 3);
    }
  }

  g.sendBuffer();
  return true;
}
