# Architecture decisions

Short, durable records. Each one exists because it was
easy to get wrong, or because a future change would
otherwise quietly undo it.

---

## D1 — Wake-word detection runs on the backend, never on the ESP32

**Status:** decided, 2026-08-28
**Scope:** OUT OF SCOPE unless explicitly revisited

The ESP32-WROOM-32 does **not** run an offline wake-word
engine. No `esp-sr` / WakeNet / Porcupine / TFLite-Micro
dependency may be added for this purpose.

**Division of labour**

| ESP32-WROOM-32 | PC / backend |
|---|---|
| I2S mic capture (INMP441) | Wake-word detection |
| Voice activity detection | Speech-to-text |
| Audio level + silence detection | LLM / reasoning |
| Audio buffering and gating | Memory, tool calling |
| Wi-Fi transport | Spotify, PC control, notifications |
| Bot state, OLED/Mochi, touch, RTC, ToF, LEDs | Text-to-speech |
| Audio playback (MAX98357A) | External APIs |

**Flow**

```
INMP441 -> ESP32 -> VAD/gating -> Wi-Fi -> backend
       -> wake-word -> STT -> AI -> tool -> TTS
       -> ESP32 -> MAX98357A -> speaker
```

If the backend does not find the wake word in a segment,
it says so and the ESP32 returns to IDLE without any
visible or audible change.

**Why:** the WROOM-32 has no PSRAM, ~320 KB of DRAM, and
203 KB of flash already committed to Mochi animation
data. VAD is cheap; a wake-word model is not.

**Consequence:** the ESP32 must never stream continuous
silence. VAD gates the uplink — that is the whole reason
VAD lives on-device rather than on the backend too.

---

## D2 — `mochi_animations_128x64.h` may be included by exactly one translation unit

**Status:** decided, 2026-08-28

The header declares its payload arrays as file-scope
`const`, which in C++ means **internal linkage**. Every
`.cpp` that includes it gets a private copy of all
203 KB.

`src/display/MochiPlayer.cpp` is the only file permitted
to include it. Everything else goes through
`MochiPlayer`'s interface, which deliberately does not
expose the animation types.

**Consequence:** a second include costs 203 KB of flash
silently, with no warning from the compiler.

---

## D3 — Mochi playback decodes incrementally

**Status:** decided, 2026-08-28

The header's own `mochiDecodeFrame()` replays every frame
from 0 on each call — ~2850 chunk decodes to play one
75-frame animation instead of 75.

`MochiPlayer::renderFrame()` keeps the frame buffer
between calls and applies only the next chunk when the
request is the next sequential frame.

This is **bit-identical**, not an approximation:
the original computes `memset; apply 0..N+1`, which is
exactly `original(N)` followed by `apply N+1`. Seeks
still replay from frame 0.

---

## D4 — The I2C bus runs at 400 kHz

**Status:** decided, 2026-08-28

The SH1106 needs 1024 bytes per frame. At the Arduino
default of 100 kHz that transfer alone costs ~92 ms,
which is longer than the 60–70 ms frame durations in the
animation header — the display bus, not the CPU, was the
original bottleneck.

Every device on this bus (SH1106, DS3231, VL53L0X) is
rated for 400 kHz.

---

## D5 — A missing peripheral must never halt the bot

**Status:** decided, 2026-08-28

The original sketch entered `while(true)` when the DS3231
was absent, taking the animation and touch down with it.

Subsystems report status to `Diagnostics` and degrade.
Only the feature that needs the missing part stops
working.

---

## D7 — Plain `ws://` only, with a hand-written client

**Status:** decided, 2026-08-28

The Links2004 `WebSockets` library includes
`<WiFiClientSecure.h>` unconditionally for
`NETWORK_ESP32` (`WebSockets.h` line 237). There is no
build flag to disable it, so linking that library drags
in the whole TLS stack even though this bot only ever
speaks `ws://` to a backend on the same LAN.

`src/net/MiniWebSocket` implements the client half of
RFC 6455 on a plain `NetworkClient` instead: handshake,
masked client frames, unmasked server frames, ping/pong,
close. No TLS surface at all.

**Measured effect**

| | Flash | % of 1,310,720 |
|---|---|---|
| Links2004 WebSockets | 1,386,672 | 105% — would not link |
| MiniWebSocket | 1,212,657 | 92% |

Saving: **174,015 bytes**. TLS symbol counts went
`ssl_tls` 5→0, `NetworkClientSecure` 41→0, `x509` 42→0.

**What this buys:** the default partition scheme is kept,
and with it OTA capability. Huge APP was explicitly
rejected.

**Not implemented, on purpose:** `wss://`,
permessage-deflate, fragmented sends, and
`Sec-WebSocket-Accept` verification. The last one is a
real (small) shortcut: we check for HTTP 101 and trust
it, because this connects to one known host on a private
network and the SHA-1 is not worth the flash.

**Consequence:** a text frame larger than 1024 bytes is
dropped rather than half-parsed, so backend control
messages must stay under that. Binary frames are
delivered in chunks, which is fine for PCM.

---

## D6 — Unwired hardware reports NOT TESTED, never OK

**Status:** decided, 2026-08-28

`Diagnostics` distinguishes `NotTested` / `Ok` / `Failed`
and defaults to `NotTested`. A subsystem may only be
marked `Ok` by code that actually talked to the device.
