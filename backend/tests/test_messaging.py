"""Event delivery, voice framing, and the control channel."""

from __future__ import annotations

import asyncio

import pytest
import websockets

from chotubot import protocol, server
from conftest import TEST_TOKEN, expect_closed, json_dumps, recv_json

pytestmark = pytest.mark.asyncio


class TestBotEvents:
    @pytest.mark.parametrize(
        "event",
        ["TOUCH_SHORT", "TOUCH_DOUBLE", "TOUCH_LONG", "TOUCH_TRIPLE",
         "PERSON_DETECTED", "PERSON_LEFT", "ALARM_FIRED", "BATTERY_LOW"],
    )
    async def test_known_events_accepted(self, bot, event):
        await bot.send(json_dumps({"type": "event", "event": event}))

        # Accepted silently; prove it by round-tripping after.
        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"

    async def test_unknown_event_rejected(self, bot):
        await bot.send(json_dumps({"type": "event", "event": "SELF_DESTRUCT"}))
        assert (await recv_json(bot))["code"] == protocol.ErrorCode.INVALID_FIELD


class TestVoiceSegments:
    async def test_audio_accounted_within_segment(self, bot):
        await bot.send(json_dumps({"type": "voice_start"}))

        # 10 blocks the size the firmware actually sends.
        block = b"\x00\x01" * 256
        for _ in range(10):
            await bot.send(block)

        await bot.send(json_dumps({"type": "voice_end"}))

        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"

        session = next(iter(server.hub.sessions))
        assert session.audio_bytes == len(block) * 10
        assert session.voice_open is False

    async def test_segment_resets_byte_count(self, bot):
        for _ in range(2):
            await bot.send(json_dumps({"type": "voice_start"}))
            await bot.send(b"\x00\x01" * 128)
            await bot.send(json_dumps({"type": "voice_end"}))

        await bot.send(json_dumps({"type": "ping"}))
        await recv_json(bot)

        session = next(iter(server.hub.sessions))
        assert session.audio_bytes == 256


class TestControlChannel:
    async def test_bad_token_closed(self, backend):
        async with websockets.connect(f"{backend}/control") as ws:
            await ws.send(json_dumps({"token": "wrong"}))
            await expect_closed(ws, protocol.CLOSE_BAD_TOKEN)

    async def test_expression_reaches_bot(self, bot, control):
        await control.send(json_dumps({"action": "expression", "value": "thinking"}))

        delivered = await recv_json(bot)
        assert delivered == {"type": "expression", "value": "thinking"}

        ack = await recv_json(control)
        assert ack["bots"] == 1

    async def test_invalid_expression_rejected_not_delivered(self, bot, control):
        await control.send(json_dumps({"action": "expression", "value": "wizard"}))

        ack = await recv_json(control)
        assert "error" in ack

        # Nothing should have reached the bot.
        with pytest.raises(asyncio.TimeoutError):
            await asyncio.wait_for(bot.recv(), 0.4)

    async def test_unknown_action_rejected(self, control):
        await control.send(json_dumps({"action": "rm_rf"}))
        assert "error" in await recv_json(control)

    async def test_notification_truncated_before_delivery(self, bot, control):
        await control.send(
            json_dumps(
                {"action": "notify", "title": "T" * 80, "body": "B" * 80}
            )
        )

        delivered = await recv_json(bot)
        assert len(delivered["title"]) == 27
        assert len(delivered["body"]) == 39

    async def test_spotify_state_reaches_bot(self, bot, control):
        await control.send(
            json_dumps(
                {
                    "action": "spotify",
                    "playing": True,
                    "track": "Blinding Lights",
                    "artist": "The Weeknd",
                    "progress_ms": 84000,
                    "duration_ms": 192000,
                }
            )
        )

        delivered = await recv_json(bot)
        assert delivered["type"] == "spotify_state"
        assert delivered["track"] == "Blinding Lights"
        assert delivered["playing"] is True

    async def test_status_lists_connected_bots(self, bot, control):
        await control.send(json_dumps({"action": "status"}))

        status = await recv_json(control)
        assert len(status["bots"]) == 1
        assert status["bots"][0]["fw"] == "pytest"

    async def test_broadcast_reaches_every_bot(self, backend, control):
        bots = []
        try:
            for _ in range(3):
                ws = await websockets.connect(f"{backend}/bot")
                await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
                await recv_json(ws)
                await recv_json(ws)
                bots.append(ws)

            await control.send(json_dumps({"action": "expression", "value": "happy"}))

            for ws in bots:
                assert (await recv_json(ws))["value"] == "happy"

            assert (await recv_json(control))["bots"] == 3
        finally:
            for ws in bots:
                await ws.close()


class TestOutboundSizeGuard:
    async def test_oversized_message_refused_not_sent(self, bot, monkeypatch):
        session = next(iter(server.hub.sessions))

        # Firmware drops text frames over 1024 bytes, so the
        # server must refuse to send one at all.
        monkeypatch.setattr(protocol, "MAX_TEXT_FRAME_BYTES", 50)
        await session.send({"type": "expression", "value": "x" * 200})

        with pytest.raises(asyncio.TimeoutError):
            await asyncio.wait_for(bot.recv(), 0.4)
