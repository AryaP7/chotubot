# Chotubot backend

Plain `ws://` server for the desktop companion. This
milestone proves the link only — no AI, no Spotify, no
audio processing.

It is testable with **no hardware soldered**: the
simulator speaks exactly what the firmware speaks.

## Setup

```bash
cd backend
pip install -r requirements.txt
```

Set a token. Anything on your LAN that knows it can drive
the bot, so make it long:

```bash
export CHOTUBOT_TOKEN="$(python -c 'import secrets;print(secrets.token_urlsafe(24))')"
```

On Windows PowerShell:

```powershell
$env:CHOTUBOT_TOKEN = python -c "import secrets;print(secrets.token_urlsafe(24))"
```

The same value goes in `sketch_bkl/src/net/Secrets.h` as
`BACKEND_TOKEN`. That file is gitignored.

## Run

Three terminals, all with `CHOTUBOT_TOKEN` set.

**1 — server**

```bash
python -m chotubot.server
```

**2 — the bot** (simulator, or a real ESP32 on your LAN)

```bash
python tools/simulate_bot.py --event TOUCH_LONG --voice 1.0
```

**3 — drive it**

```bash
python tools/console.py
```

Then type `happy`, `thinking`, `notify Build done | All
tests pass`, or `spotify Blinding Lights | The Weeknd`.

## What the link does today

```
bot  -> {"type":"hello","token":"...","fw":"..."}
     <- {"type":"expression","value":"happy"}
bot  -> {"type":"ping"}
     <- {"type":"pong"}
bot  -> {"type":"event","event":"TOUCH_LONG"}
bot  -> {"type":"voice_start"} .. binary PCM .. {"type":"voice_end"}
```

Backend → bot commands: `expression`, `state`, `wake`,
`notification`, `spotify_state`, plus binary PCM for
playback.

## Security posture

Honest about what this is: a personal device on a private
network.

- **Token auth**, compared with `secrets.compare_digest`
- **Allowlists, not filters** — unknown message types,
  events, expressions and states are rejected by name
- **Separate paths** — `/bot` for devices, `/control` for
  operators; neither can be mistaken for the other
- **Audio only inside a segment** — binary frames outside
  `voice_start`/`voice_end` are dropped
- **Outbound size checked** against the firmware's 1024
  byte text-frame limit, so a too-long message is caught
  here rather than silently dropped on the device

**Not** provided: transport encryption. Traffic is
plaintext by choice (DECISIONS.md D7). Do not expose this
port to the internet.

## Adding a service

The two hooks are marked `# HOOK:` in `server.py` — one
where physical events arrive, one where a completed voice
segment lands. Build commands with `protocol.py`'s
constructors rather than raw dicts; they validate, and an
invalid command then cannot be constructed at all.
