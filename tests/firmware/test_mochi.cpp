// MochiPlayer - the real decoder and frame timing.
//
// This file includes the animation header directly so it
// can reach the header's own mochiDecodeFrame() and compare
// it against MochiPlayer's incremental path. That means the
// test binary carries two copies of the 203 KB payload --
// irrelevant on a PC, and the reason DECISIONS.md D2 forbids
// it in firmware.
//
// Nothing here says anything about the SH1106. The decoder
// is HOST-VERIFIED; the display is HARDWARE-UNVERIFIED.

#include "support/fixture.h"
#include "support/tinytest.h"

#include "src/display/MochiPlayer.h"

#include "mochi_animations_128x64.h"

namespace {

MochiPlayer make() {
  hostsim::resetAll();
  MochiPlayer player;
  player.begin();
  return player;
}

}  // namespace

TEST(Mochi, table_matches_the_documented_contents) {
  MochiPlayer player = make();
  CHECK_EQ(player.animationCount(), 18);
  CHECK_EQ(MOCHI_BYTES_PER_FRAME, 1024);
  CHECK_EQ(MOCHI_WIDTH, 128);
  CHECK_EQ(MOCHI_HEIGHT, 64);
}

TEST(Mochi, frame_counts_sum_to_the_documented_raw_size) {
  uint32_t frames = 0;
  for (uint16_t i = 0; i < MOCHI_ANIMATION_COUNT; ++i) {
    frames += mochi_animations[i]->frameCount;
  }

  CHECK_EQ(frames, 1383u);
  CHECK_EQ(frames * MOCHI_BYTES_PER_FRAME, MOCHI_RAW_FRAME_BYTES);
}

TEST(Mochi, select_by_name_finds_real_animations) {
  MochiPlayer player = make();

  CHECK(player.selectByName("happy"));
  CHECK_STR_EQ(player.currentName(), "happy");

  CHECK(player.selectByName("music"));
  CHECK_STR_EQ(player.currentName(), "music");
}

TEST(Mochi, unknown_name_is_rejected_and_changes_nothing) {
  MochiPlayer player = make();
  player.selectByName("happy");

  CHECK(!player.selectByName("wizard"));
  CHECK_STR_EQ(player.currentName(), "happy");
}

TEST(Mochi, out_of_range_index_is_ignored) {
  MochiPlayer player = make();
  player.selectIndex(3);
  const uint16_t before = player.animationIndex();

  player.selectIndex(999);
  CHECK_EQ(player.animationIndex(), before);
}

TEST(Mochi, advance_wraps_around_the_table) {
  MochiPlayer player = make();
  player.selectIndex(MOCHI_ANIMATION_COUNT - 1);

  player.advanceAnimation();
  CHECK_EQ(player.animationIndex(), 0);
  CHECK_EQ(player.frameIndex(), 0);
}

TEST(Mochi, incremental_decode_is_bit_identical_to_the_header) {
  // DECISIONS.md D3 claims MochiPlayer's O(1) incremental
  // path produces exactly what the header's O(n) decoder
  // does. This proves it, frame by frame.
  MochiPlayer player = make();

  const char* sample[] = {"happy", "angry_2", "intro", "sleepy_3"};
  uint8_t reference[MOCHI_BYTES_PER_FRAME];

  for (const char* name : sample) {
    CHECK(player.selectByName(name));

    const MochiAnimation& anim = *mochi_animations[player.animationIndex()];
    player.restart(0);

    uint32_t now = 0;
    for (uint16_t frame = 0; frame < anim.frameCount; ++frame) {
      // The header's own decoder, replayed from zero.
      CHECK(mochiDecodeFrame(anim, frame, reference));

      CHECK_EQ(player.frameIndex(), frame);
      CHECK_EQ(memcmp(player.frameBuffer(), reference, MOCHI_BYTES_PER_FRAME), 0);

      // Step the clock past this frame's duration.
      now += mochiFrameDuration(anim, frame) + 1;
      hostsim::setMillis(now);
      player.update(now);
    }
  }
}

TEST(Mochi, seeking_backwards_still_matches_the_header) {
  MochiPlayer player = make();
  player.selectByName("determined");

  const MochiAnimation& anim = *mochi_animations[player.animationIndex()];
  uint8_t reference[MOCHI_BYTES_PER_FRAME];

  // Play forward a way, then restart - which forces the
  // slow rebuild path rather than the incremental one.
  player.restart(0);
  for (uint16_t i = 0; i < 20; ++i) {
    hostsim::advanceMillis(200);
    player.update(millis());
  }

  player.selectByName("determined");   // resets frame to 0
  player.restart(millis());

  CHECK(mochiDecodeFrame(anim, 0, reference));
  CHECK_EQ(memcmp(player.frameBuffer(), reference, MOCHI_BYTES_PER_FRAME), 0);
}

TEST(Mochi, frame_advances_only_after_its_duration) {
  MochiPlayer player = make();
  player.selectByName("happy");
  player.restart(0);

  const MochiAnimation& anim = *mochi_animations[player.animationIndex()];
  const uint16_t duration = mochiFrameDuration(anim, 0);

  hostsim::setMillis(duration - 1);
  CHECK(!player.update(millis()));
  CHECK_EQ(player.frameIndex(), 0);

  hostsim::setMillis(duration);
  CHECK(player.update(millis()));
  CHECK_EQ(player.frameIndex(), 1);
}

TEST(Mochi, auto_advance_moves_to_the_next_animation_at_the_end) {
  MochiPlayer player = make();
  player.selectIndex(0);
  player.setAutoAdvance(true);
  player.restart(0);

  const uint16_t frames = mochi_animations[0]->frameCount;

  uint32_t now = 0;
  for (uint16_t i = 0; i < frames; ++i) {
    now += 500;
    hostsim::setMillis(now);
    player.update(now);
  }

  CHECK_EQ(player.animationIndex(), 1);
  CHECK_EQ(player.frameIndex(), 0);
}

TEST(Mochi, pinned_animation_loops_instead_of_advancing) {
  MochiPlayer player = make();
  player.selectIndex(0);
  player.setAutoAdvance(false);
  player.restart(0);

  const uint16_t frames = mochi_animations[0]->frameCount;

  uint32_t now = 0;
  for (uint16_t i = 0; i < frames + 5; ++i) {
    now += 500;
    hostsim::setMillis(now);
    player.update(now);
  }

  CHECK_EQ(player.animationIndex(), 0);
}

TEST(Mochi, set_expression_pins_a_real_animation) {
  MochiPlayer player = make();

  CHECK(player.setExpression(Expression::Music, true));
  CHECK_STR_EQ(player.currentName(), "music");
  CHECK(!player.autoAdvance());

  CHECK(player.setExpression(Expression::Idle, false));
  CHECK(player.autoAdvance());
}

TEST(Mochi, blit_transposes_rows_into_u8g2_pages) {
  // The optimisation that replaced u8g2's per-pixel
  // drawBitmap. Every output bit must equal the matching
  // input bit.
  MochiPlayer player = make();
  player.selectByName("happy");
  player.restart(0);

  // Step to a frame with plenty of set pixels.
  for (int i = 0; i < 8; ++i) {
    hostsim::advanceMillis(200);
    player.update(millis());
  }

  uint8_t page[1024];
  memset(page, 0xAA, sizeof(page));   // poison, must be fully overwritten
  player.blitTo(page);

  const uint8_t* src = player.frameBuffer();
  int mismatches = 0;
  int setBits = 0;

  for (uint8_t p = 0; p < 8; ++p) {
    for (uint16_t x = 0; x < 128; ++x) {
      for (uint8_t b = 0; b < 8; ++b) {
        const uint16_t y = p * 8 + b;
        const bool source = (src[y * 16 + x / 8] >> (7 - (x % 8))) & 1;
        const bool dest = (page[p * 128 + x] >> b) & 1;

        if (source != dest) ++mismatches;
        if (source) ++setBits;
      }
    }
  }

  CHECK_EQ(mismatches, 0);
  CHECK(setBits > 0);   // a blank frame would prove nothing
}

TEST(Mochi, blit_rejects_a_null_target) {
  MochiPlayer player = make();
  player.blitTo(nullptr);   // must not crash
  CHECK(true);
}

TEST(Mochi, every_animation_decodes_its_first_and_last_frame) {
  hostsim::resetAll();
  uint8_t reference[MOCHI_BYTES_PER_FRAME];

  for (uint16_t i = 0; i < MOCHI_ANIMATION_COUNT; ++i) {
    const MochiAnimation& anim = *mochi_animations[i];

    CHECK(mochiDecodeFrame(anim, 0, reference));
    CHECK(mochiDecodeFrame(anim, anim.frameCount - 1, reference));

    // Out of range must fail rather than read past the end.
    CHECK(!mochiDecodeFrame(anim, anim.frameCount, reference));
  }
}

TEST(Mochi, frame_durations_are_all_plausible) {
  for (uint16_t i = 0; i < MOCHI_ANIMATION_COUNT; ++i) {
    const MochiAnimation& anim = *mochi_animations[i];

    for (uint16_t f = 0; f < anim.frameCount; ++f) {
      const uint16_t duration = mochiFrameDuration(anim, f);
      CHECK(duration > 0);
      CHECK(duration < 5000);
    }
  }
}
