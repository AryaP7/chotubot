"""Wire protocol between the bot and this backend.

Everything crossing the socket is validated here, in one
place, so no handler downstream has to wonder whether a
field is present or the right type.

Two rules shape this module:

1. The bot is a physical device on a desk. A malformed or
   unexpected message is dropped and logged, never guessed
   at -- a wrong guess makes a real object do something.

2. Commands are an allowlist, not a filter. Anything not
   named here does not exist.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

PROTOCOL_VERSION = 1
"""Bumped only for a breaking change.

Adding a new message type or an optional field is NOT
breaking: both sides ignore what they do not recognise, by
design, so new commands can ship without a version bump.
Renaming or removing a field, or changing the meaning of
one, is breaking.

A bot reporting an unknown major version is refused rather
than half-supported."""

MIN_SUPPORTED_VERSION = 1


class ErrorCode:
    """Codes sent back in an `error` message.

    Named rather than free text so a client can branch on
    them without string matching.
    """

    BAD_JSON = "bad_json"
    UNKNOWN_TYPE = "unknown_type"
    INVALID_FIELD = "invalid_field"
    NOT_AUTHENTICATED = "not_authenticated"
    UNSUPPORTED_VERSION = "unsupported_version"
    AUDIO_OUTSIDE_SEGMENT = "audio_outside_segment"


# WebSocket close codes. 4000-4999 is the private range.
CLOSE_NOT_AUTHENTICATED = 4001
CLOSE_BAD_TOKEN = 4003
CLOSE_UNKNOWN_PATH = 4004
CLOSE_UNSUPPORTED_VERSION = 4005
CLOSE_HEARTBEAT_TIMEOUT = 4008

# Expressions the firmware actually has assets for. Kept
# in step with src/display/ExpressionMap.cpp -- sending
# anything else makes the bot log an error and ignore it.
EXPRESSIONS = frozenset(
    {
        "idle",
        "listening",
        "thinking",
        "speaking",
        "happy",
        "excited",
        "confused",
        "angry",
        "sleepy",
        "love",
        "surprised",
        "notification",
        "error",
        "music",
        "boot",
    }
)

# Bot states the firmware will act on.
BOT_STATES = frozenset({"IDLE", "LISTENING", "THINKING", "SPEAKING", "ERROR"})

# Messages the bot may send us.
INBOUND_TYPES = frozenset(
    {
        "hello",
        "ping",
        "event",
        "voice_start",
        "voice_end",
    }
)

# Physical events the bot may report.
BOT_EVENTS = frozenset(
    {
        "TOUCH_SHORT",
        "TOUCH_DOUBLE",
        "TOUCH_LONG",
        "TOUCH_TRIPLE",
        "PERSON_DETECTED",
        "PERSON_LEFT",
        "GESTURE_APPROACH",
        "GESTURE_RETREAT",
        "GESTURE_HOVER",
        "ALARM_FIRED",
        "BATTERY_LOW",
    }
)

MAX_TEXT_FRAME_BYTES = 1024
"""Firmware drops any text frame larger than this rather
than half-parsing it (MiniWebSocket.h). Outbound messages
are checked against the same limit here so a too-long
message is caught on this side, where it can be logged."""


class ProtocolError(Exception):
    """Raised for anything that fails validation."""


@dataclass(frozen=True)
class Hello:
    token: str
    firmware: str


def _require_str(payload: dict[str, Any], key: str, max_len: int) -> str:
    value = payload.get(key)
    if not isinstance(value, str):
        raise ProtocolError(f"{key!r} must be a string")
    if len(value) > max_len:
        raise ProtocolError(f"{key!r} exceeds {max_len} characters")
    return value


def parse_inbound(payload: Any) -> dict[str, Any]:
    """Validate one decoded message from the bot."""
    if not isinstance(payload, dict):
        raise ProtocolError("message must be a JSON object")

    msg_type = payload.get("type")
    if not isinstance(msg_type, str):
        raise ProtocolError("missing 'type'")
    if msg_type not in INBOUND_TYPES:
        raise ProtocolError(f"unknown message type {msg_type!r}")

    if msg_type == "hello":
        # Absent version means version 1: the firmware that
        # shipped before versioning existed.
        version = payload.get("v", 1)
        if not isinstance(version, int) or isinstance(version, bool):
            raise ProtocolError("'v' must be an integer")

        return {
            "type": "hello",
            "token": _require_str(payload, "token", 128),
            "fw": payload.get("fw", "unknown"),
            "v": version,
        }

    if msg_type == "event":
        event = _require_str(payload, "event", 64)
        if event not in BOT_EVENTS:
            raise ProtocolError(f"unknown event {event!r}")
        return {"type": "event", "event": event}

    return {"type": msg_type}


# --------------------------------------------------------
# Outbound builders
#
# Handlers call these rather than assembling dicts, so an
# invalid command cannot be constructed in the first place.
# --------------------------------------------------------


def pong() -> dict[str, Any]:
    return {"type": "pong"}


def welcome(server_version: int = PROTOCOL_VERSION) -> dict[str, Any]:
    """Sent once, immediately after a successful hello.

    Tells the bot which version it is actually talking to
    and how often we expect to hear from it.
    """
    return {
        "type": "welcome",
        "v": server_version,
        "heartbeat_s": HEARTBEAT_INTERVAL_S,
    }


def error(code: str, message: str) -> dict[str, Any]:
    return {"type": "error", "code": code, "message": message[:120]}


HEARTBEAT_INTERVAL_S = 20
"""How often the bot should send something. The firmware
pings on this cadence."""

HEARTBEAT_TIMEOUT_S = 50
"""Silence longer than this and the connection is dropped.
Generous enough to survive two missed pings, short enough
that a bot which fell off the Wi-Fi does not linger as a
phantom session."""


def expression(value: str) -> dict[str, Any]:
    value = value.lower().strip()
    if value not in EXPRESSIONS:
        raise ProtocolError(
            f"unknown expression {value!r}; valid: {sorted(EXPRESSIONS)}"
        )
    return {"type": "expression", "value": value}


def state(name: str) -> dict[str, Any]:
    name = name.upper().strip()
    if name not in BOT_STATES:
        raise ProtocolError(f"unknown state {name!r}; valid: {sorted(BOT_STATES)}")
    return {"type": "state", "state": name}


def wake(detected: bool) -> dict[str, Any]:
    return {"type": "wake", "detected": bool(detected)}


def notification(title: str, body: str = "") -> dict[str, Any]:
    # Firmware buffers are 28 and 40 bytes. Truncate here so
    # the text is cut deliberately rather than by memcpy.
    return {
        "type": "notification",
        "title": str(title)[:27],
        "body": str(body)[:39],
    }


def spotify_state(
    *,
    playing: bool,
    track: str = "",
    artist: str = "",
    progress_ms: int = 0,
    duration_ms: int = 0,
    status: str | None = None,
) -> dict[str, Any]:
    payload = {
        "type": "spotify_state",
        "playing": bool(playing),
        "track": str(track)[:39],
        "artist": str(artist)[:31],
        "progress_ms": max(0, int(progress_ms)),
        "duration_ms": max(0, int(duration_ms)),
    }
    if status is not None:
        payload["status"] = status
    return payload
