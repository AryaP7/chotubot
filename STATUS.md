# Chotubot — current status

Last updated: 2026-08-28

Read this before assuming anything works. Terms are used
strictly: **VERIFIED** means someone observed it on real
hardware; **COMPILES** means the toolchain accepted it and
nothing more.

---

## Headline

| Component | Status |
|---|---|
| Backend | **VERIFIED** — 218 automated tests |
| Host firmware logic | **HOST-VERIFIED** — 110 automated tests |
| Firmware build | **COMPILES** — 1,230,785 bytes (93%), RAM 55,392 (16%) |
| Physical ESP32 | **UNVERIFIED** — never flashed |
| SH1106 display | **UNVERIFIED** |
| TTP223B touch | **UNVERIFIED** |
| DS3231 clock | **UNVERIFIED** |
| INMP441 microphone | **UNVERIFIED** — not connected |
| MAX98357A + speaker | **UNVERIFIED** — not connected |
| VL53L0X proximity | **UNVERIFIED** — not connected |
| WS2812B lighting | **UNVERIFIED** — not connected |
| Power system | **UNVERIFIED** — not built |
| Real LLM provider | **UNVERIFIED** — no credentials have ever been available |
| Real Spotify API | **UNVERIFIED** — no credentials have ever been available |

### What HOST-VERIFIED means

The C++ executes correctly on a PC with the Arduino layer
stubbed. It says **nothing** about the ESP32: not about
timing under a real scheduler, not about I²C, not about
whether any peripheral is even attached. A module can be
HOST-VERIFIED and still fail on first flash.

---

## Physical hardware

**Present and previously working (V1, pre-refactor):**

- ESP32-WROOM-32
- Touch Sensor (TTP223B) — GPIO4
- OLED Display (SH1106) — I²C, GPIO21 / GPIO22
- RTC Module (DS3231) — I²C, GPIO21 / GPIO22

The refactored firmware has **not** been flashed to this
hardware. The V1 behaviour it must preserve is:

    Mochi (all 18 animations cycling)
      -> touch -> clock -> touch -> Mochi

Note the touch behaviour has deliberately widened since
V1: a single tap now cycles Mochi → Clock → Now Playing →
Diagnostics, and a **double** tap is the direct
Mochi/Clock toggle. A tap now resolves on release rather
than on press, because a press cannot yet be told apart
from a long press.

**Not yet bought, wired or tested:**

- INMP441 microphone
- MAX98357A amplifier + speaker
- VL53L0X time-of-flight sensor
- WS2812B LED strip
- LiPo, TP4056, MT3608, slide switch, passives

Every driver for these compiles and reports `NOT TESTED`
in diagnostics rather than `OK`.

---

## Firmware subsystems

| Subsystem | Code | Hardware |
|---|---|---|
| Mochi player, compressed, incremental decode | COMPILES | UNVERIFIED |
| SH1106 display + direct blit | COMPILES | UNVERIFIED |
| Touch gestures — short/double/long/triple | COMPILES | UNVERIFIED |
| DS3231 clock | COMPILES | UNVERIFIED |
| Alarms, quiet hours, time-of-day | COMPILES | UNVERIFIED |
| Event bus + state machine | COMPILES | UNVERIFIED |
| Diagnostics screen | COMPILES | UNVERIFIED |
| VL53L0X presence | COMPILES | NO HARDWARE |
| ToF gestures | COMPILES | **DISABLED** — untuned, serial `g` |
| WS2812B lighting | COMPILES | NO HARDWARE |
| INMP441 + VAD | COMPILES | NO HARDWARE |
| MAX98357A output | COMPILES | NO HARDWARE |
| Battery monitor | COMPILES | NO CIRCUIT |
| Wi-Fi manager | COMPILES | UNVERIFIED |
| Backend client (`ws://`) | COMPILES | UNVERIFIED |

---

## AI agent — Phase E

**Implemented and tested. Backend only; no firmware change.**

```
LLMProvider  ->  Agent  ->  ToolRegistry  ->  Tool
                              |
                          BotGateway  ->  Protocol v1  ->  ESP32
```

| Component | State |
|---|---|
| `Agent` loop with tool iteration cap | tested |
| `Conversation` / `Message` / store | tested |
| `ToolRegistry` name + argument validation | tested |
| `BotGateway` (Hub / Null) | tested |
| Tools: `get_time`, `get_bot_status`, `set_expression` | tested |
| `FakeLLMProvider`, `ScriptedProvider`, `FailingProvider` | tested |
| Chat over the `/control` channel | tested |

**Provider-agnostic:** the Agent imports no vendor SDK.
`anthropic_provider.py` is the only module that does.

| Provider | State |
|---|---|
| `FakeLLMProvider` | offline keyword rules — the default |
| `ScriptedProvider` / `FailingProvider` | test doubles |
| `AnthropicProvider` | **IMPLEMENTED, NEVER RUN LIVE** — no credentials available |

Selected by `LLM_PROVIDER`, which defaults to `fake` so
nothing starts spending money by accident.

**Known limitation — thinking blocks are not echoed.** The
Agent stores history as plain text, so `thinking` blocks
from a response are dropped rather than replayed on the
next turn. The API accepts this; continuity across a
multi-turn tool sequence may be slightly worse than
echoing them. Fixing it means letting `Message` carry
provider-native blocks.

**Not implemented:** STT, TTS, memory persistence,
PC control. Phases F, H, I.

---

## Spotify — Phase G

**Backend only. No firmware change.**

```
Agent -> ToolRegistry -> Spotify tools -> SpotifyService -> Web API
                              |
                        BotGateway -> Protocol v1 -> ESP32
```

| Layer | Status |
|---|---|
| `SpotifyService` interface + 10 tools | **IMPLEMENTED / TESTED** |
| `FakeSpotifyService` | **TESTED** — deterministic, offline |
| `SpotifyWebApi` (real client) | **IMPLEMENTED, UNVERIFIED** — never called live |
| Spotify → `BotGateway` → Protocol v1 | **TESTED** over a real socket |
| ESP32 handling of `spotify_state` | **HOST-UNTESTED, HARDWARE-UNVERIFIED** |
| Physical OLED now-playing screen | **UNVERIFIED** |

Tools: `spotify_play`, `_pause`, `_resume`, `_next`,
`_previous`, `_now_playing`, `_search`, `_play_track`,
`_play_album`, `_play_playlist`.

**Security.** The model supplies a search phrase or a
22-character base62 id — never a URL. Every endpoint is a
literal constant in `web_api.py`. IDs are validated before
any request is built, so `../`, `?`, `/` and full URLs are
rejected. Credentials live in the environment; the ESP32
receives only track, artist, progress, duration and status.

**Known limits.** Playback control is a Spotify **Premium**
API — a free account gets a clear error rather than a
silent no-op. Spotify also cannot start from nothing: some
device must already be open, or the tool reports
`no active Spotify device` with instructions.

---

## Host firmware tests

**110 tests, 4,832 assertions, passing.** Deterministic —
three consecutive runs produce identical counts.

```bash
python tools/test_firmware.py
python tools/test_firmware.py --filter Mochi
```

Needs no board, no USB, no Arduino IDE, no network, no
sensors. Requires only a host C++ compiler; the runner
finds g++ on PATH or where winget installs WinLibs.

| Module | Tests | What actually ran |
|---|---|---|
| `MochiPlayer` | 16 | Decoder, frame timing, the page blit |
| `WebSocket` | 16 | Real RFC 6455 handshake, masking, framing |
| `VAD` | 14 | Real RMS, adaptive floor, hysteresis |
| `StateMachine` | 21 | Real transition table |
| `TouchInput` | 12 | Real gesture recogniser on a fake clock |
| `Proximity` | 12 | Real hysteresis and confirmation |
| `EventBus` | 9 | Real ring buffer |
| `Battery` | 6 | Real discharge curve |
| `Expressions` | 4 | Real allowlist and parser |

Two claims that were previously assertions are now
**proved by execution**:

- **DECISIONS.md D3** — MochiPlayer's O(1) incremental
  decode is bit-identical to the header's own O(n)
  decoder, checked frame-by-frame across four animations
  including the 105-frame `intro`.
- **The blit optimisation** — every one of the 8,192 bits
  in a transposed frame matches its source bit.

**Not covered on the host:** anything needing real
hardware — `DisplayManager`, `ClockScreen`,
`NowPlayingScreen`, `NotificationScreen`, `Diagnostics`,
`RtcManager`, `Scheduler` (NVS), `WifiManager`,
`BackendClient` (ArduinoJson), `AudioOutput`, and
`sketch_bkl.ino` itself.

---

## Backend tests

**218 automated tests, passing, ~5 s.** Run them with:

```bash
cd backend && python -m pytest
```

Offline by default — no API key, no account, no network.
Ten live tests are opt-in and skip without credentials:

```bash
pytest -m live           # 4 tests, real LLM
pytest -m spotify_live   # 6 tests, real Spotify
```

**Neither has ever run — no credentials have been
available for either service.**

Stable across repeated runs — no flakes observed in three
consecutive runs. They exercise a real server on a real
ephemeral socket; the transport is not mocked.

| Area | Covered |
|---|---|
| Agent loop | init, tool selection, multi-turn, iteration cap |
| Agent failures | provider error/timeout/crash, empty reply, bad tool, bad args |
| Tool registry | unknown name, missing/extra/wrong-typed args, enum |
| Agent ↔ Protocol v1 | expression reaches a live socket; offline is honest |
| Protocol validation | 18 tests — parsing, allowlists, truncation |
| Authentication | token, pre-hello commands, unknown path, hello timeout |
| Versioning | future version refused, missing version = v1 |
| Heartbeat | ping/pong, silent drop, traffic keeps alive |
| Error handling | bad JSON, unknown type, audio outside segment |
| Reconnect | repeated cycles, cleanup, reconnect after heartbeat drop |
| Events | all 8 bot event types |
| Voice framing | byte accounting, per-segment reset |
| Control channel | delivery, rejection, truncation, broadcast to 3 bots |
| Outbound guard | oversized frame refused before sending |

**Not covered:** the hardware-facing firmware modules
listed above, and the physical behaviour of every
peripheral.

---

## Backend

**Implemented and exercised** (`backend/`):

- Plain `ws://` server, no TLS (see DECISIONS.md D7)
- Token authentication with `compare_digest`
- Allowlist validation on every message and command
- `/bot` for devices, `/control` for desktop tools
- Voice segment framing and byte accounting
- Bot simulator — makes all of the above testable with
  nothing soldered
- Control console

**Protocol v1 — complete for Phase B:**

| Feature | State |
|---|---|
| Versioning | `hello` carries `v`; server refuses unsupported |
| Handshake | server replies `welcome` with version + heartbeat |
| Heartbeat | bot pings every 20 s, server drops after 50 s silence |
| Errors | `{"type":"error","code":...}` with named codes |
| Close codes | 4001/4003/4004/4005/4008, each distinct |
| Reconnect | firmware backoff; server releases sessions cleanly |
| Extensibility | unknown types ignored both ways, so new commands need no version bump |

**Verified working, simulator ↔ backend:**

    -> hello (v1)         <- welcome: protocol v1, heartbeat 20s
                          <- expression: happy
    -> ping               <- pong
    -> event TOUCH_LONG
    -> voice_start / 16000 bytes / voice_end
    wrong token           <- rejected [4003] bad token
    v99                   <- error unsupported_version, closed [4005]
    silence               <- closed [4008] heartbeat timeout
    unknown message type  <- error, link survives

**Not implemented at all:** wake-word detection, STT, LLM,
TTS, memory, tool calling, Spotify, PC control,
notification sources. The protocol has hooks where these
attach; none of them exist.

---

## Constraints in force

- Partition scheme stays **default** — OTA is preserved.
  Huge APP was explicitly rejected.
- Mochi data stays **compressed in flash**. Not moving to
  LittleFS.
- ~80 KB flash headroom is accepted. No further flash
  optimisation unless a build actually fails.
- No offline wake word on the ESP32, ever (DECISIONS.md D1).
- Battery reports millivolts as measurement and a
  curve-derived percentage as an estimate — the display
  marks it `~`.

---

## Next milestone

Backend services, in order, only after the link is proven
against real hardware:

1. Flash the firmware, confirm V1 behaviour survives
2. Wake-word detection on the backend
3. STT → LLM → TTS round trip
4. Spotify
5. PC control
