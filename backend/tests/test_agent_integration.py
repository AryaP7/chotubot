"""The agent driving a real bot connection over Protocol v1.

Everything below runs against a live server and a live
WebSocket. The "bot" is a socket speaking the firmware's
protocol -- no hardware, but no shortcuts either.
"""

from __future__ import annotations

import asyncio

import pytest

from chotubot import server
from conftest import json_dumps, recv_json

pytestmark = pytest.mark.asyncio


@pytest.fixture(autouse=True)
def fresh_conversations():
    """Agent state is module-level; isolate the tests."""
    yield
    for cid in server.agent.store.list_ids():
        server.agent.store.delete(cid)


class TestChatOverControlChannel:
    async def test_chat_returns_structured_response(self, control):
        await control.send(
            json_dumps({"action": "chat", "text": "hello", "conversation_id": "t1"})
        )
        reply = await recv_json(control)

        assert reply["ok"] is True
        assert reply["conversation_id"] == "t1"
        assert isinstance(reply["tools_used"], list)
        assert reply["text"]

    async def test_chat_selects_and_runs_a_tool(self, control):
        await control.send(
            json_dumps(
                {"action": "chat", "text": "what time is it?", "conversation_id": "t2"}
            )
        )
        reply = await recv_json(control)

        assert [t["name"] for t in reply["tools_used"]] == ["get_time"]
        assert reply["tools_used"][0]["ok"] is True

    async def test_agent_expression_reaches_a_connected_bot(self, bot, control):
        """The whole point of the milestone.

        chat -> agent -> tool -> gateway -> Protocol v1 -> bot
        """
        await control.send(
            json_dumps(
                {"action": "chat", "text": "look happy", "conversation_id": "t3"}
            )
        )

        delivered = await recv_json(bot)
        assert delivered == {"type": "expression", "value": "happy"}

        reply = await recv_json(control)
        assert reply["ok"] is True
        assert [t["name"] for t in reply["tools_used"]] == ["set_expression"]

    async def test_agent_reports_bot_status_truthfully(self, bot, control):
        await control.send(
            json_dumps(
                {"action": "chat", "text": "what is your status?",
                 "conversation_id": "t4"}
            )
        )
        reply = await recv_json(control)

        assert "1 bot(s) connected" in reply["text"]
        assert "pytest" in reply["text"]

    async def test_agent_reports_offline_when_no_bot(self, control):
        await control.send(
            json_dumps(
                {"action": "chat", "text": "what is your status?",
                 "conversation_id": "t5"}
            )
        )
        reply = await recv_json(control)

        assert "No bot is connected" in reply["text"]

    async def test_expression_with_no_bot_does_not_error(self, control):
        await control.send(
            json_dumps(
                {"action": "chat", "text": "look happy", "conversation_id": "t6"}
            )
        )
        reply = await recv_json(control)

        # Honest, not a failure: nothing displayed it.
        assert reply["ok"] is True
        assert "No bot is connected" in reply["text"]

    async def test_multi_turn_keeps_context(self, control):
        for text in ["hello", "what time is it?"]:
            await control.send(
                json_dumps(
                    {"action": "chat", "text": text, "conversation_id": "t7"}
                )
            )
            await recv_json(control)

        conversation = server.agent.store.get("t7")
        assert conversation is not None
        assert len(conversation.messages) >= 5

    async def test_reset_clears_conversation(self, control):
        await control.send(
            json_dumps({"action": "chat", "text": "hello", "conversation_id": "t8"})
        )
        await recv_json(control)

        await control.send(
            json_dumps({"action": "reset", "conversation_id": "t8"})
        )
        assert (await recv_json(control))["ok"] is True

        assert len(server.agent.store.get("t8").messages) == 1

    async def test_empty_chat_is_rejected_not_crashed(self, control):
        await control.send(json_dumps({"action": "chat", "text": ""}))
        reply = await recv_json(control)

        assert reply["ok"] is False
        assert reply["error"] == "empty input"


class TestResilience:
    async def test_bot_disconnecting_mid_conversation(self, backend, control):
        import websockets

        from conftest import TEST_TOKEN

        ws = await websockets.connect(f"{backend}/bot")
        await ws.send(json_dumps({"type": "hello", "token": TEST_TOKEN}))
        await recv_json(ws)
        await recv_json(ws)

        await ws.close()
        for _ in range(40):
            if not server.hub.sessions:
                break
            await asyncio.sleep(0.02)

        # The agent must notice, not hang or crash.
        await control.send(
            json_dumps(
                {"action": "chat", "text": "look happy", "conversation_id": "t9"}
            )
        )
        reply = await recv_json(control)

        assert reply["ok"] is True
        assert "No bot is connected" in reply["text"]

    async def test_server_still_serves_protocol_after_agent_use(self, bot, control):
        await control.send(
            json_dumps({"action": "chat", "text": "hello", "conversation_id": "t10"})
        )
        await recv_json(control)

        # Protocol v1 unaffected by agent traffic.
        await bot.send(json_dumps({"type": "ping"}))
        assert (await recv_json(bot))["type"] == "pong"
