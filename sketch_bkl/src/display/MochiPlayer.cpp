#include "MochiPlayer.h"

// The ONE place this header may be included. See the
// linkage note in MochiPlayer.h before adding another.
#include "../../mochi_animations_128x64.h"

static_assert(kMochiFrameBytes == MOCHI_BYTES_PER_FRAME,
              "Frame buffer size is out of step with the animation header");

namespace {

// Apply a single frame's chunk on top of whatever the
// buffer already holds.
//
// RLE chunks are whole-frame writes, PATCH chunks are
// deltas against the previous frame. This mirrors the
// body of the header's own decode loop for one
// iteration, so replaying frames 0..N through here is
// byte-identical to mochiDecodeFrame(N).
void applyFrame(const MochiAnimation& anim, uint16_t frame, uint8_t* out) {
  const uint32_t start = mochiRead32(anim.offsets + frame);
  const uint32_t end = mochiRead32(anim.offsets + frame + 1);
  const uint8_t mode = mochiRead8(anim.modes + frame);

  const uint8_t* chunk = anim.data + start;
  const uint16_t len = static_cast<uint16_t>(end - start);

  if (mode == MOCHI_FRAME_RLE) {
    mochiDecodeRle(chunk, len, out);
  } else {
    mochiApplyPatch(chunk, len, out);
  }
}

}  // namespace

void MochiPlayer::begin() {
  animIndex_ = 0;
  frameIndex_ = 0;
  bufferAnim_ = 0xFFFF;
  bufferFrame_ = 0xFFFF;
}

uint16_t MochiPlayer::animationCount() const {
  return MOCHI_ANIMATION_COUNT;
}

const char* MochiPlayer::currentName() const {
  return mochi_animations[animIndex_]->name;
}

bool MochiPlayer::selectByName(const char* name) {
  for (uint16_t i = 0; i < MOCHI_ANIMATION_COUNT; ++i) {
    if (strcmp(mochi_animations[i]->name, name) == 0) {
      selectIndex(i);
      return true;
    }
  }
  return false;
}

void MochiPlayer::selectIndex(uint16_t index) {
  if (index >= MOCHI_ANIMATION_COUNT) return;
  animIndex_ = index;
  frameIndex_ = 0;
}

bool MochiPlayer::setExpression(Expression expression, bool pinned) {
  setAutoAdvance(!pinned);
  return selectByName(animationFor(expression));
}

void MochiPlayer::blitTo(uint8_t* dest) const {
  if (dest == nullptr) return;

  // Source: row-major, 16 bytes per row, MSB is leftmost.
  // Dest:   u8g2 pages - dest[page * 128 + x] holds the
  //         8 vertical pixels (page*8 .. page*8+7) of
  //         column x, bit 0 topmost.
  for (uint8_t page = 0; page < 8; ++page) {
    const uint8_t* src = buffer_ + static_cast<uint16_t>(page) * 8 * 16;
    uint8_t* out = dest + static_cast<uint16_t>(page) * 128;

    for (uint8_t bx = 0; bx < 16; ++bx) {
      // The 8 source bytes covering this 8x8 block.
      const uint8_t s0 = src[bx];
      const uint8_t s1 = src[bx + 16];
      const uint8_t s2 = src[bx + 32];
      const uint8_t s3 = src[bx + 48];
      const uint8_t s4 = src[bx + 64];
      const uint8_t s5 = src[bx + 80];
      const uint8_t s6 = src[bx + 96];
      const uint8_t s7 = src[bx + 112];

      uint8_t* col = out + static_cast<uint16_t>(bx) * 8;

      for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t m = static_cast<uint8_t>(0x80 >> i);
        uint8_t v = 0;
        if (s0 & m) v |= 0x01;
        if (s1 & m) v |= 0x02;
        if (s2 & m) v |= 0x04;
        if (s3 & m) v |= 0x08;
        if (s4 & m) v |= 0x10;
        if (s5 & m) v |= 0x20;
        if (s6 & m) v |= 0x40;
        if (s7 & m) v |= 0x80;
        col[i] = v;
      }
    }
  }
}

void MochiPlayer::advanceAnimation() {
  animIndex_ = static_cast<uint16_t>((animIndex_ + 1) % MOCHI_ANIMATION_COUNT);
  frameIndex_ = 0;
}

void MochiPlayer::renderFrame(uint16_t frame) {
  const MochiAnimation& anim = *mochi_animations[animIndex_];
  if (frame >= anim.frameCount) return;

  // Fast path: the buffer already holds the frame
  // immediately before this one, in this same animation,
  // so a single chunk brings it up to date.
  //
  // This is what makes playback O(1) per frame instead
  // of O(n). The header's mochiDecodeFrame() replays
  // every frame from 0 on each call, which for a
  // 75-frame animation is ~2850 chunk decodes per loop
  // rather than 75.
  if (frame > 0 && bufferAnim_ == animIndex_ && bufferFrame_ == frame - 1) {
    applyFrame(anim, frame, buffer_);
    bufferFrame_ = frame;
    return;
  }

  // Seek path: rebuild from the start of the animation.
  // Identical to the header's own decoder.
  memset(buffer_, 0, kMochiFrameBytes);
  for (uint16_t f = 0; f <= frame; ++f) {
    applyFrame(anim, f, buffer_);
  }

  bufferAnim_ = animIndex_;
  bufferFrame_ = frame;
}

void MochiPlayer::restart(uint32_t nowMs) {
  renderFrame(frameIndex_);
  lastFrameMs_ = nowMs;
}

bool MochiPlayer::update(uint32_t nowMs) {
  const MochiAnimation& anim = *mochi_animations[animIndex_];

  const uint16_t duration = mochiFrameDuration(anim, frameIndex_);

  if (nowMs - lastFrameMs_ < duration) {
    return false;
  }

  lastFrameMs_ = nowMs;
  ++frameIndex_;

  if (frameIndex_ >= anim.frameCount) {
    frameIndex_ = 0;

    if (autoAdvance_) {
      advanceAnimation();

      Serial.print(F("[mochi] "));
      Serial.println(currentName());
    }
  }

  renderFrame(frameIndex_);
  return true;
}
