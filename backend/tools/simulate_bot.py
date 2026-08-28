"""Pretend to be the ESP32.

This is what makes the backend testable with nothing
soldered. It speaks exactly the protocol the firmware
speaks -- same hello, same events, same voice framing --
and prints what the backend sends back.

    python tools/simulate_bot.py
    python tools/simulate_bot.py --event TOUCH_LONG
    python tools/simulate_bot.py --voice 1.5

Anything this rejects, the real firmware would reject too.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import os
import struct
import sys

import websockets

DEFAULT_URI = os.environ.get("CHOTUBOT_URI", "ws://127.0.0.1:8080/bot")
TOKEN = os.environ.get("CHOTUBOT_TOKEN", "change-me")

# Kept in step with the firmware's kProtocolVersion and
# protocol.PROTOCOL_VERSION.
PROTOCOL_VERSION = 1

SAMPLE_RATE = 16000

# Mirrors MiniWebSocket::kRxBufferSize. If the backend
# sends a text frame larger than this, the real bot drops
# it -- so this drops it too, loudly.
RX_LIMIT = 1024


def make_tone(seconds: float, freq: float = 220.0) -> bytes:
    """int16 mono PCM, the shape a real voice segment has."""
    count = int(SAMPLE_RATE * seconds)
    samples = [
        int(9000 * math.sin(2 * math.pi * freq * (i / SAMPLE_RATE)))
        for i in range(count)
    ]
    return struct.pack(f"<{len(samples)}h", *samples)


async def reader(ws: websockets.ClientConnection) -> None:
    try:
        await _read_loop(ws)
    except websockets.ConnectionClosed:
        # Expected whenever the backend hangs up; the main
        # coroutine reports why.
        pass


async def _read_loop(ws: websockets.ClientConnection) -> None:
    async for raw in ws:
        if isinstance(raw, bytes):
            secs = len(raw) / 2 / SAMPLE_RATE
            print(f"  <- [audio] {len(raw)} bytes (~{secs:.2f}s) -> would play")
            continue

        if len(raw.encode()) > RX_LIMIT:
            print(f"  <- [DROPPED] text frame {len(raw)} bytes exceeds {RX_LIMIT}")
            continue

        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            print(f"  <- [bad JSON] {raw!r}")
            continue

        kind = msg.get("type")

        if kind == "welcome":
            print(
                f"  <- welcome: protocol v{msg.get('v')}, "
                f"heartbeat every {msg.get('heartbeat_s')}s"
            )
        elif kind == "error":
            print(f"  <- error [{msg.get('code')}]: {msg.get('message')}")
        elif kind == "expression":
            print(f"  <- expression: {msg.get('value')}  [Mochi would change]")
        elif kind == "pong":
            print("  <- pong")
        elif kind == "state":
            print(f"  <- state: {msg.get('state')}")
        elif kind == "notification":
            print(f"  <- notification: {msg.get('title')} / {msg.get('body')}")
        elif kind == "spotify_state":
            playing = "playing" if msg.get("playing") else "paused"
            print(f"  <- spotify [{playing}]: {msg.get('track')} - {msg.get('artist')}")
        elif kind == "wake":
            print(f"  <- wake detected: {msg.get('detected')}")
        else:
            print(f"  <- {raw}")


async def run(args: argparse.Namespace) -> int:
    print(f"connecting to {args.uri}")

    try:
        async with websockets.connect(args.uri) as ws:
            await ws.send(
                json.dumps(
                    {
                        "type": "hello",
                        "token": args.token,
                        "fw": "simulator",
                        "v": PROTOCOL_VERSION,
                    }
                )
            )
            print(f"  -> hello (v{PROTOCOL_VERSION})")

            task = asyncio.create_task(reader(ws))
            await asyncio.sleep(0.4)

            await ws.send(json.dumps({"type": "ping"}))
            print("  -> ping")
            await asyncio.sleep(0.4)

            if args.event:
                await ws.send(json.dumps({"type": "event", "event": args.event}))
                print(f"  -> event {args.event}")
                await asyncio.sleep(0.4)

            if args.voice:
                pcm = make_tone(args.voice)
                await ws.send(json.dumps({"type": "voice_start"}))
                print(f"  -> voice_start")

                # Chunked the way the firmware chunks it:
                # one VAD block at a time.
                block = 256 * 2
                for i in range(0, len(pcm), block):
                    await ws.send(pcm[i : i + block])
                    await asyncio.sleep(0.001)

                await ws.send(json.dumps({"type": "voice_end"}))
                print(f"  -> voice_end ({len(pcm)} bytes)")
                await asyncio.sleep(0.4)

            print(f"\nidling for {args.hold}s -- drive it with tools/console.py\n")
            await asyncio.sleep(args.hold)

            task.cancel()

    except websockets.ConnectionClosed as exc:
        # The backend closes with a reason. Report that
        # rather than dumping a traceback -- a rejected
        # token is an expected outcome, not a crash.
        reason = exc.rcvd.reason if exc.rcvd else "connection closed"
        code = exc.rcvd.code if exc.rcvd else "?"
        print(f"  <- rejected by backend [{code}]: {reason}")
        return 1
    except OSError as exc:
        print(f"could not connect: {exc}")
        return 1
    except websockets.InvalidStatus as exc:
        print(f"rejected: {exc}")
        return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Simulate the Chotubot ESP32.")
    parser.add_argument("--uri", default=DEFAULT_URI)
    parser.add_argument("--token", default=TOKEN)
    parser.add_argument("--event", help="send one event, e.g. TOUCH_SHORT")
    parser.add_argument(
        "--voice", type=float, metavar="SECONDS", help="send a fake voice segment"
    )
    parser.add_argument("--hold", type=float, default=30.0)
    return asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    sys.exit(main())
