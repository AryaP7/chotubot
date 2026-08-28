"""Chotubot backend -- plain ws:// server for the local LAN.

Deliberately small. This milestone proves the link works
and nothing more: no STT, no LLM, no TTS, no Spotify.
Those attach to the hooks marked below once this is solid.

Security posture: this binds to the LAN and expects a
shared token. That stops a stray browser tab or another
device on the network driving the bot by accident. It is
not transport security -- traffic is unencrypted by
design (see DECISIONS.md D7), so run it on a network you
control.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import os
import secrets
import time
from typing import Any

import websockets
from websockets.asyncio.server import ServerConnection, serve

from . import protocol
from .agent import HubGateway, build_agent
from .protocol import ProtocolError

log = logging.getLogger("chotubot")

HOST = os.environ.get("CHOTUBOT_HOST", "0.0.0.0")
PORT = int(os.environ.get("CHOTUBOT_PORT", "8080"))
PATH = os.environ.get("CHOTUBOT_PATH", "/bot")
TOKEN = os.environ.get("CHOTUBOT_TOKEN", "change-me")

# Desktop tools (console, future services) attach here.
# Same token, separate path, so bots and operators are
# never confused for one another.
CONTROL_PATH = os.environ.get("CHOTUBOT_CONTROL_PATH", "/control")

# A client gets this long to say hello before being cut.
HELLO_TIMEOUT_S = 5.0


class Session:
    """One connected bot."""

    def __init__(self, connection: ServerConnection) -> None:
        self.connection = connection
        self.authenticated = False
        self.firmware = "unknown"
        self.version = 0
        self.peer = connection.remote_address[0] if connection.remote_address else "?"
        self.audio_bytes = 0
        self.voice_open = False
        self.last_seen = time.monotonic()

    def touch(self) -> None:
        """Mark the connection as alive."""
        self.last_seen = time.monotonic()

    @property
    def silent_for(self) -> float:
        return time.monotonic() - self.last_seen

    async def send_error(self, code: str, message: str) -> None:
        await self.send(protocol.error(code, message))

    async def send(self, payload: dict[str, Any]) -> None:
        text = json.dumps(payload, separators=(",", ":"))

        # The firmware drops oversized text frames, so catch
        # it here where we can say why.
        encoded = text.encode()
        if len(encoded) > protocol.MAX_TEXT_FRAME_BYTES:
            log.error(
                "refusing to send %d byte message (firmware limit %d): %.60s...",
                len(encoded),
                protocol.MAX_TEXT_FRAME_BYTES,
                text,
            )
            return

        await self.connection.send(text)
        log.debug("-> %s", text)

    async def send_audio(self, pcm: bytes) -> None:
        await self.connection.send(pcm)


class Hub:
    """Tracks connected bots so tools can address them."""

    def __init__(self) -> None:
        self.sessions: set[Session] = set()

    def add(self, session: Session) -> None:
        self.sessions.add(session)

    def discard(self, session: Session) -> None:
        self.sessions.discard(session)

    async def broadcast(self, payload: dict[str, Any]) -> int:
        targets = [s for s in self.sessions if s.authenticated]
        for session in targets:
            try:
                await session.send(payload)
            except websockets.ConnectionClosed:
                pass
        return len(targets)


hub = Hub()

# The agent reaches bots through the gateway, never through
# `hub` directly, so tools stay testable with no sockets.
# Provider is the offline fake until a real one is wired in;
# swapping it changes nothing else.
agent = build_agent(HubGateway(hub))


async def handle_message(session: Session, raw: str) -> None:
    # Any well-formed traffic counts as a heartbeat.
    session.touch()

    try:
        payload = json.loads(raw)
    except json.JSONDecodeError:
        log.warning("%s sent non-JSON: %.80s", session.peer, raw)
        if session.authenticated:
            await session.send_error(protocol.ErrorCode.BAD_JSON, "not valid JSON")
        return

    try:
        message = protocol.parse_inbound(payload)
    except ProtocolError as exc:
        log.warning("%s sent invalid message (%s): %.120s", session.peer, exc, raw)
        if session.authenticated:
            await session.send_error(protocol.ErrorCode.INVALID_FIELD, str(exc))
        return

    kind = message["type"]

    # Everything except hello requires authentication.
    if not session.authenticated and kind != "hello":
        log.warning("%s sent %r before hello, closing", session.peer, kind)
        await session.connection.close(
            code=protocol.CLOSE_NOT_AUTHENTICATED, reason="not authenticated"
        )
        return

    if kind == "hello":
        # compare_digest so a wrong token cannot be found
        # by timing the reply.
        if not secrets.compare_digest(message["token"], TOKEN):
            log.warning("%s failed authentication", session.peer)
            await session.connection.close(
                code=protocol.CLOSE_BAD_TOKEN, reason="bad token"
            )
            return

        version = int(message.get("v", 1))
        if not (protocol.MIN_SUPPORTED_VERSION <= version <= protocol.PROTOCOL_VERSION):
            log.warning("%s speaks protocol v%d, unsupported", session.peer, version)
            await session.send_error(
                protocol.ErrorCode.UNSUPPORTED_VERSION,
                f"server supports v{protocol.MIN_SUPPORTED_VERSION}"
                f"-v{protocol.PROTOCOL_VERSION}, got v{version}",
            )
            await session.connection.close(
                code=protocol.CLOSE_UNSUPPORTED_VERSION, reason="unsupported version"
            )
            return

        session.authenticated = True
        session.version = version
        session.firmware = str(message.get("fw", "unknown"))[:40]
        log.info(
            "%s authenticated (fw=%s, v%d)", session.peer, session.firmware, version
        )

        await session.send(protocol.welcome())
        await session.send(protocol.expression("happy"))
        return

    if kind == "ping":
        await session.send(protocol.pong())
        return

    if kind == "event":
        log.info("%s event: %s", session.peer, message["event"])
        # HOOK: physical events reach the assistant here.
        return

    if kind == "voice_start":
        session.voice_open = True
        session.audio_bytes = 0
        log.info("%s voice segment opened", session.peer)
        return

    if kind == "voice_end":
        session.voice_open = False
        log.info(
            "%s voice segment closed (%d bytes, ~%.2fs @16kHz mono)",
            session.peer,
            session.audio_bytes,
            session.audio_bytes / 2 / 16000,
        )
        # HOOK: wake-word detection, then STT, goes here.
        # See DECISIONS.md D1 -- the bot never decides this.
        return


async def heartbeat_watchdog(session: Session) -> None:
    """Drop a connection that has gone quiet.

    A TCP socket can stay open long after the device on the
    far end has vanished -- the bot walks out of Wi-Fi range
    and nothing tells us. Without this, dead sessions
    accumulate and commands are broadcast into nothing.
    """
    try:
        while True:
            await asyncio.sleep(protocol.HEARTBEAT_INTERVAL_S / 2)

            if session.silent_for > protocol.HEARTBEAT_TIMEOUT_S:
                log.warning(
                    "%s silent for %.0fs, dropping",
                    session.peer,
                    session.silent_for,
                )
                await session.connection.close(
                    code=protocol.CLOSE_HEARTBEAT_TIMEOUT, reason="heartbeat timeout"
                )
                return
    except asyncio.CancelledError:
        raise
    except websockets.ConnectionClosed:
        pass


async def handle_control(connection: ServerConnection) -> None:
    """A desktop tool driving the bot.

    Commands are built through protocol.py's constructors,
    so a control client cannot invent a message the
    firmware would not understand.
    """
    peer = connection.remote_address[0] if connection.remote_address else "?"
    authed = False
    log.info("control client %s connected", peer)

    try:
        async for raw in connection:
            if isinstance(raw, bytes):
                continue

            try:
                payload = json.loads(raw)
            except json.JSONDecodeError:
                await connection.send(json.dumps({"error": "not JSON"}))
                continue

            if not authed:
                if not secrets.compare_digest(str(payload.get("token", "")), TOKEN):
                    await connection.close(code=4003, reason="bad token")
                    return
                authed = True
                await connection.send(json.dumps({"ok": "authenticated"}))
                continue

            action = payload.get("action")

            try:
                if action == "expression":
                    command = protocol.expression(str(payload.get("value", "")))
                elif action == "state":
                    command = protocol.state(str(payload.get("value", "")))
                elif action == "wake":
                    command = protocol.wake(bool(payload.get("detected", True)))
                elif action == "notify":
                    command = protocol.notification(
                        str(payload.get("title", "")), str(payload.get("body", ""))
                    )
                elif action == "spotify":
                    command = protocol.spotify_state(
                        playing=bool(payload.get("playing", False)),
                        track=str(payload.get("track", "")),
                        artist=str(payload.get("artist", "")),
                        progress_ms=int(payload.get("progress_ms", 0)),
                        duration_ms=int(payload.get("duration_ms", 0)),
                    )
                elif action == "chat":
                    # Runs the agent. Any tool it picks may
                    # reach the bot through the gateway, so
                    # this one action can produce outbound
                    # Protocol v1 traffic on its own.
                    reply = await agent.send(
                        str(payload.get("text", "")),
                        conversation_id=payload.get("conversation_id"),
                    )
                    await connection.send(json.dumps(reply.to_dict()))
                    continue

                elif action == "reset":
                    ok = agent.reset(str(payload.get("conversation_id", "")))
                    await connection.send(json.dumps({"ok": ok}))
                    continue

                elif action == "status":
                    await connection.send(
                        json.dumps(
                            {
                                "bots": [
                                    {"peer": s.peer, "fw": s.firmware}
                                    for s in hub.sessions
                                    if s.authenticated
                                ]
                            }
                        )
                    )
                    continue
                else:
                    await connection.send(
                        json.dumps({"error": f"unknown action {action!r}"})
                    )
                    continue

            except ProtocolError as exc:
                await connection.send(json.dumps({"error": str(exc)}))
                continue

            sent = await hub.broadcast(command)
            await connection.send(json.dumps({"ok": command, "bots": sent}))

    except websockets.ConnectionClosed:
        pass
    finally:
        log.info("control client %s disconnected", peer)


async def handle_connection(connection: ServerConnection) -> None:
    path = connection.request.path

    if path == CONTROL_PATH:
        await handle_control(connection)
        return

    if path != PATH:
        log.warning("rejected connection to %s", path)
        await connection.close(code=4004, reason="unknown path")
        return

    session = Session(connection)
    hub.add(session)
    log.info("%s connected", session.peer)

    try:
        async with asyncio.timeout(HELLO_TIMEOUT_S):
            first = await connection.recv()
            if isinstance(first, bytes):
                raise ProtocolError("expected hello, got binary")
            await handle_message(session, first)

        if not session.authenticated:
            return

        watchdog = asyncio.create_task(heartbeat_watchdog(session))

        try:
            async for raw in connection:
                if isinstance(raw, bytes):
                    session.touch()

                    # Voice uplink. Only accepted inside an
                    # open segment, so a firmware bug cannot
                    # flood us.
                    if session.voice_open:
                        session.audio_bytes += len(raw)
                    else:
                        log.warning(
                            "%s sent %d audio bytes outside a segment, dropped",
                            session.peer,
                            len(raw),
                        )
                        await session.send_error(
                            protocol.ErrorCode.AUDIO_OUTSIDE_SEGMENT,
                            "send voice_start first",
                        )
                    continue

                await handle_message(session, raw)
        finally:
            watchdog.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await watchdog

    except TimeoutError:
        log.warning("%s never said hello, closing", session.peer)
    except websockets.ConnectionClosed:
        pass
    except ProtocolError as exc:
        log.warning("%s protocol error: %s", session.peer, exc)
    finally:
        hub.discard(session)
        log.info("%s disconnected", session.peer)


async def main() -> None:
    logging.basicConfig(
        level=os.environ.get("CHOTUBOT_LOG", "INFO").upper(),
        format="%(asctime)s  %(levelname)-7s %(message)s",
        datefmt="%H:%M:%S",
    )

    if TOKEN == "change-me":
        log.warning("CHOTUBOT_TOKEN is unset -- using the default token")

    log.info("listening on ws://%s:%d%s", HOST, PORT, PATH)

    async with serve(handle_connection, HOST, PORT):
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
