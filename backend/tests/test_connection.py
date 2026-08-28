"""Connection lifecycle: auth, versioning, heartbeat, errors.

Everything here runs against a real server on a real
socket. Nothing is stubbed.
"""

from __future__ import annotations

import asyncio

import pytest
import websockets

from chotubot import protocol, server
from conftest import TEST_TOKEN, expect_closed, json_dumps, recv_json

pytestmark = pytest.mark.asyncio


class TestAuthentication:
    async def test_valid_token_gets_welcome(self, backend):
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(
                json_dumps({"type": "hello", "token": TEST_TOKEN, "v": 1})
            )
            welcome = await recv_json(ws)

            assert welcome["type"] == "welcome"
            assert welcome["v"] == protocol.PROTOCOL_VERSION
            assert welcome["heartbeat_s"] == protocol.HEARTBEAT_INTERVAL_S

    async def test_bad_token_closed(self, backend):
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": "wrong"}))
            await expect_closed(ws, protocol.CLOSE_BAD_TOKEN)

    async def test_command_before_hello_closed(self, backend):
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "ping"}))
            await expect_closed(ws, protocol.CLOSE_NOT_AUTHENTICATED)

    async def test_unknown_path_closed(self, backend):
        async with websockets.connect(f"{backend}/nonsense") as ws:
            await expect_closed(ws, protocol.CLOSE_UNKNOWN_PATH)

    async def test_silence_before_hello_closed(self, backend, monkeypatch):
        monkeypatch.setattr(server, "HELLO_TIMEOUT_S", 0.3)
        async with websockets.connect(f"{backend}/bot") as ws:
            await expect_closed(ws, timeout=3.0)


class TestVersioning:
    async def test_future_version_refused_with_reason(self, backend):
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(
                json_dumps({"type": "hello", "token": TEST_TOKEN, "v": 99})
            )

            error = await recv_json(ws)
            assert error["type"] == "error"
            assert error["code"] == protocol.ErrorCode.UNSUPPORTED_VERSION

            await expect_closed(ws, protocol.CLOSE_UNSUPPORTED_VERSION)

    async def test_missing_version_treated_as_v1(self, backend):
        # Firmware predating versioning must still connect.
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
            assert (await recv_json(ws))["type"] == "welcome"


class TestHeartbeat:
    async def test_ping_answered_with_pong(self, bot):
        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"

    async def test_silent_connection_dropped(self, backend, monkeypatch):
        # Compress the real timings so the test is quick.
        monkeypatch.setattr(protocol, "HEARTBEAT_INTERVAL_S", 0.2)
        monkeypatch.setattr(protocol, "HEARTBEAT_TIMEOUT_S", 0.3)

        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
            await recv_json(ws)  # welcome
            await recv_json(ws)  # expression

            # Say nothing and wait to be dropped.
            await expect_closed(ws, protocol.CLOSE_HEARTBEAT_TIMEOUT, timeout=5.0)

    async def test_traffic_keeps_connection_alive(self, backend, monkeypatch):
        monkeypatch.setattr(protocol, "HEARTBEAT_INTERVAL_S", 0.2)
        monkeypatch.setattr(protocol, "HEARTBEAT_TIMEOUT_S", 0.6)

        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
            await recv_json(ws)
            await recv_json(ws)

            for _ in range(5):
                await asyncio.sleep(0.2)
                await ws.send(json_dumps({"type": "ping"}))
                assert (await recv_json(ws))["type"] == "pong"


class TestErrorHandling:
    async def test_bad_json_reports_error_and_survives(self, bot):
        await bot.send("{not json at all")

        error = await recv_json(bot)
        assert error["type"] == "error"
        assert error["code"] == protocol.ErrorCode.BAD_JSON

        # The link must still work afterwards.
        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"

    async def test_unknown_type_reports_error_and_survives(self, bot):
        await bot.send(json_dumps({"type": "launch_missiles"}))

        error = await recv_json(bot)
        assert error["code"] == protocol.ErrorCode.INVALID_FIELD

        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"

    async def test_audio_outside_segment_rejected(self, bot):
        await bot.send(b"\x00\x01" * 128)

        error = await recv_json(bot)
        assert error["code"] == protocol.ErrorCode.AUDIO_OUTSIDE_SEGMENT

        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"


async def wait_for_sessions(count: int, timeout: float = 2.0) -> None:
    """Server-side cleanup unwinds asynchronously after the
    client closes, so poll rather than assert immediately."""
    deadline = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < deadline:
        if len(server.hub.sessions) == count:
            return
        await asyncio.sleep(0.02)

    assert len(server.hub.sessions) == count, (
        f"expected {count} sessions, found {len(server.hub.sessions)}"
    )


class TestReconnect:
    async def test_bot_can_reconnect_after_drop(self, backend):
        for _ in range(3):
            async with websockets.connect(f"{backend}/bot") as ws:
                await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
                assert (await recv_json(ws))["type"] == "welcome"
                await recv_json(ws)

            # Each connection must be fully released before
            # the next, or sessions accumulate.
            await wait_for_sessions(0)

    async def test_session_removed_on_disconnect(self, backend):
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
            await recv_json(ws)
            await recv_json(ws)
            assert len(server.hub.sessions) == 1

        await wait_for_sessions(0)

    async def test_reconnect_after_heartbeat_drop(self, backend, monkeypatch):
        """The realistic failure: bot goes quiet, gets
        dropped, then comes back."""
        monkeypatch.setattr(protocol, "HEARTBEAT_INTERVAL_S", 0.2)
        monkeypatch.setattr(protocol, "HEARTBEAT_TIMEOUT_S", 0.3)

        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
            await recv_json(ws)
            await recv_json(ws)
            await expect_closed(ws, protocol.CLOSE_HEARTBEAT_TIMEOUT, timeout=5.0)

        await wait_for_sessions(0)

        # Same bot reconnects and is served normally.
        async with websockets.connect(f"{backend}/bot") as ws:
            await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
            assert (await recv_json(ws))["type"] == "welcome"
