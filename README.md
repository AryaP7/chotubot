# Chotubot

A desktop companion robot: an ESP32 with a face, and a
Python backend that gives it a brain.

The ESP32 is the body — display, touch, clock, and later a
microphone, speaker, proximity sensor and lights. Anything
expensive (wake word, speech-to-text, LLM, text-to-speech,
Spotify, PC control) runs on a PC and talks to the board
over plain `ws://` on the local network.

> **Status: nothing has been flashed yet.** The firmware
> compiles and its logic is tested on a PC, but no physical
> board has run it. [STATUS.md](STATUS.md) tracks exactly
> what is verified and what is not — read it before
> assuming anything works.

## Layout

```
sketch_bkl/          ESP32 firmware (Arduino, C++)
  src/               modular: core, display, input, audio,
                     sensors, lighting, net, rtc, power
backend/             Python backend
  chotubot/          protocol, server, agent, spotify
  tools/             simulator, console, demos
tests/firmware/      host-side C++ tests with Arduino stubbed
tools/               test_firmware.py
```

## Running it

Backend tests — no hardware, no network, no API keys:

```bash
cd backend && python -m pytest
```

Firmware logic on this PC — no board, needs only g++:

```bash
python tools/test_firmware.py
```

See the whole agent loop drive a simulated bot:

```bash
cd backend && python tools/demo_agent.py
```

Live services are opt-in and skipped without credentials:

```bash
cd backend && python -m pytest -m live
cd backend && python -m pytest -m spotify_live
```

## Hardware

Built and previously working: ESP32-WROOM-32, SH1106 OLED,
TTP223B touch, DS3231 RTC — sharing I²C on GPIO21/22, touch
on GPIO4.

Planned: INMP441 microphone, MAX98357A + speaker, VL53L0X
proximity, WS2812B lighting, LiPo with TP4056 and MT3608.

Wiring, passives and the soldering order are in
[STATUS.md](STATUS.md); the reasoning behind the awkward
decisions is in [DECISIONS.md](DECISIONS.md).

## Configuration

Copy the templates and fill them in — both are gitignored:

```bash
cp backend/.env.example backend/.env
cp sketch_bkl/src/net/Secrets.example.h sketch_bkl/src/net/Secrets.h
```

The firmware holds only the Wi-Fi credentials and a shared
backend token. API keys for the LLM and Spotify live on the
backend and never reach the board.
