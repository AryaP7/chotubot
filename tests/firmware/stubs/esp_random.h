// Host stub of esp_random().
//
// Deterministic on purpose: MiniWebSocket uses it for the
// handshake nonce and the frame masking key, and a test
// that asserts on masked bytes needs a repeatable sequence.

#pragma once

#include <cstdint>

namespace hostsim {
extern uint32_t g_randomSeed;
inline void seedRandom(uint32_t seed) { g_randomSeed = seed; }
}  // namespace hostsim

inline uint32_t esp_random() {
  // xorshift32 - small, deterministic, good enough for a mask.
  hostsim::g_randomSeed ^= hostsim::g_randomSeed << 13;
  hostsim::g_randomSeed ^= hostsim::g_randomSeed >> 17;
  hostsim::g_randomSeed ^= hostsim::g_randomSeed << 5;
  return hostsim::g_randomSeed;
}
