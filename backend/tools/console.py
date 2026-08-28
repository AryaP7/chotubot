"""Drive a connected bot from the keyboard.

    python tools/console.py

Commands:
    happy | thinking | listening | ...   set the expression
    state THINKING                       set bot state
    wake on | wake off                   wake-word verdict
    notify Title | Body text             push a notification
    spotify Track | Artist               fake now-playing
    pause                                fake paused
    chat What time is it?                run the AI agent
    reset                                clear the conversation
    status                               list connected bots
    quit

Works against the simulator or a real ESP32 -- the
backend does not know or care which is on the other end.
"""

from __future__ import annotations

import asyncio
import json
import os
import sys
from pathlib import Path

# Python puts this script's directory on sys.path, not the
# working directory, so the package needs adding explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import websockets  # noqa: E402

from chotubot.protocol import EXPRESSIONS  # noqa: E402

DEFAULT_URI = os.environ.get("CHOTUBOT_CONTROL_URI", "ws://127.0.0.1:8080/control")
TOKEN = os.environ.get("CHOTUBOT_TOKEN", "change-me")


def parse(line: str) -> dict | None:
    line = line.strip()
    if not line:
        return None

    head, _, rest = line.partition(" ")
    head = head.lower()
    rest = rest.strip()

    if head in EXPRESSIONS:
        return {"action": "expression", "value": head}

    if head == "state":
        return {"action": "state", "value": rest}

    if head == "wake":
        return {"action": "wake", "detected": rest.lower() not in {"off", "false", "no"}}

    if head == "notify":
        title, _, body = rest.partition("|")
        return {"action": "notify", "title": title.strip(), "body": body.strip()}

    if head == "spotify":
        track, _, artist = rest.partition("|")
        return {
            "action": "spotify",
            "playing": True,
            "track": track.strip(),
            "artist": artist.strip(),
            "progress_ms": 84000,
            "duration_ms": 192000,
        }

    if head == "pause":
        return {"action": "spotify", "playing": False, "track": "", "artist": ""}

    if head in {"chat", "ask"}:
        # Runs the agent, which may pick a tool and drive
        # the bot on its own.
        return {"action": "chat", "text": rest, "conversation_id": "console"}

    if head == "reset":
        return {"action": "reset", "conversation_id": "console"}

    if head == "status":
        return {"action": "status"}

    print(f"  ? unknown command {head!r}")
    return None


async def run() -> int:
    print(f"connecting to {DEFAULT_URI}")

    try:
        async with websockets.connect(DEFAULT_URI) as ws:
            await ws.send(json.dumps({"token": TOKEN}))
            print(await ws.recv())
            print("\nready. try: happy | thinking | notify Build done | Tests pass\n")

            loop = asyncio.get_running_loop()

            while True:
                line = await loop.run_in_executor(None, sys.stdin.readline)
                if not line:
                    break

                if line.strip().lower() in {"quit", "exit"}:
                    break

                command = parse(line)
                if command is None:
                    continue

                await ws.send(json.dumps(command))
                print(f"  {await ws.recv()}")

    except OSError as exc:
        print(f"could not connect: {exc}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(run()))
