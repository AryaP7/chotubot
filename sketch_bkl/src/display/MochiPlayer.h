#pragma once
#include <Arduino.h>

#include "ExpressionMap.h"

// =====================================================
// MOCHI PLAYER
//
// Owns animation selection, frame timing and the single
// decompressed frame buffer.
//
// IMPORTANT - FLASH BUDGET
// mochi_animations_128x64.h declares its payload arrays
// as file-scope `const`, which in C++ gives them
// INTERNAL linkage. Every translation unit that includes
// that header gets its own private copy of all 203 KB.
// MochiPlayer.cpp is the ONLY file allowed to include
// it. That is why this header exposes a plain interface
// and does not include the animation data.
// =====================================================

// 128 * 64 / 8. Mirrors MOCHI_BYTES_PER_FRAME without
// pulling in the animation payload.
static constexpr uint16_t kMochiFrameBytes = 1024;

class MochiPlayer {
 public:
  void begin();

  // Select by name as it appears in the header, e.g.
  // "happy". Returns false and leaves selection
  // untouched if the name does not exist.
  bool selectByName(const char* name);

  void selectIndex(uint16_t index);

  // Advance to the next animation in the table, wrapping.
  void advanceAnimation();

  // true  - on finishing an animation, move to the next
  //         one in the table. This is the V1 idle
  //         behaviour and stays the default.
  // false - loop the current animation forever, used
  //         when a bot state pins an expression.
  void setAutoAdvance(bool enabled) { autoAdvance_ = enabled; }
  bool autoAdvance() const { return autoAdvance_; }

  // Re-decode the current frame and reset frame timing.
  // Call after returning from another screen.
  void restart(uint32_t nowMs);

  // Non-blocking. Returns true when a new frame was
  // decoded and the buffer changed.
  bool update(uint32_t nowMs);

  // Public expression API. Other modules ask for a mood
  // and never touch animations or frames.
  //   pinned = true  hold this one animation
  //   pinned = false let it run on through the table
  bool setExpression(Expression expression, bool pinned = true);

  const uint8_t* frameBuffer() const { return buffer_; }

  // Transpose the current frame straight into a u8g2
  // full-screen buffer.
  //
  // u8g2's own drawBitmap walks the image one pixel at a
  // time through u8g2_DrawPixel - 8192 calls per frame,
  // each re-checking clipping. Our source is always the
  // full 128x64 screen, so all of that work is provably
  // unnecessary. This converts 8x8 blocks directly and
  // skips clearBuffer() too, since every byte is written.
  void blitTo(uint8_t* u8g2Buffer) const;

  const char* currentName() const;

  uint16_t animationIndex() const { return animIndex_; }
  uint16_t frameIndex() const { return frameIndex_; }
  uint16_t animationCount() const;

 private:
  // Materialise `frame` of the current animation into
  // buffer_, reusing the buffer when the request is the
  // natural next frame.
  void renderFrame(uint16_t frame);

  uint8_t buffer_[kMochiFrameBytes] = {0};

  uint16_t animIndex_ = 0;
  uint16_t frameIndex_ = 0;

  // What buffer_ currently holds, so renderFrame() can
  // tell an incremental step from a seek.
  uint16_t bufferAnim_ = 0xFFFF;
  uint16_t bufferFrame_ = 0xFFFF;

  uint32_t lastFrameMs_ = 0;
  bool autoAdvance_ = true;
};
