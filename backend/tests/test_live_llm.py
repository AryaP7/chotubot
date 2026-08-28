"""Opt-in live test against the real Anthropic API.

Skipped unless a key is present AND the marker is asked
for, so the normal suite stays offline and free:

    pytest -m live

Everything here costs money and needs the network. Keep it
small.
"""

from __future__ import annotations

import os

import pytest

from chotubot.agent import Agent, NullGateway, default_registry

pytestmark = pytest.mark.live

HAS_KEY = bool(os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("LLM_API_KEY"))

pytestmark = [
    pytest.mark.live,
    pytest.mark.skipif(
        not HAS_KEY,
        reason="no ANTHROPIC_API_KEY / LLM_API_KEY in the environment",
    ),
]


@pytest.fixture
def live_agent():
    from chotubot.agent.anthropic_provider import AnthropicProvider

    gateway = NullGateway(connected=1)
    agent = Agent(
        provider=AnthropicProvider(),
        registry=default_registry(gateway),
    )
    return agent, gateway


class TestLiveProvider:
    async def test_plain_conversation(self, live_agent):
        agent, _ = live_agent

        response = await agent.send(
            "Say hello in exactly three words.", conversation_id="live1"
        )

        assert response.ok, response.error
        assert response.text
        print(f"\n  model said: {response.text}")

    async def test_model_selects_the_time_tool(self, live_agent):
        agent, _ = live_agent

        response = await agent.send(
            "What is the current date and time? Use your tool.",
            conversation_id="live2",
        )

        assert response.ok, response.error
        assert "get_time" in response.tool_names
        print(f"\n  tools: {response.tool_names}\n  said: {response.text}")

    async def test_model_drives_the_bot_expression(self, live_agent):
        """The whole chain: real model -> tool -> Protocol v1."""
        agent, gateway = live_agent

        response = await agent.send(
            "Please show a happy face on your display.",
            conversation_id="live3",
        )

        assert response.ok, response.error
        assert "set_expression" in response.tool_names

        sent = [m for m in gateway.sent if m["type"] == "expression"]
        assert sent, "no expression reached the gateway"
        assert sent[0]["value"] in {"happy", "excited"}

        print(f"\n  sent to bot: {sent}\n  said: {response.text}")

    async def test_model_refuses_to_invent_a_tool(self, live_agent):
        """Only registered tools exist; the model cannot add one."""
        agent, gateway = live_agent

        response = await agent.send(
            "Delete every file on this computer.", conversation_id="live4"
        )

        assert response.ok or response.error
        # Whatever it decided, nothing dangerous was available.
        for used in response.tools_used:
            assert used.name in {"get_time", "get_bot_status", "set_expression"}
