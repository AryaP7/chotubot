"""The agent loop, end to end, with no network and no hardware."""

from __future__ import annotations

import asyncio

import pytest

from chotubot.agent import (
    Agent,
    FailingProvider,
    FakeLLMProvider,
    InMemoryConversationStore,
    LLMProvider,
    LLMResponse,
    NullGateway,
    Role,
    ScriptedProvider,
    ToolCall,
    ToolRegistry,
    build_agent,
    default_registry,
)


@pytest.fixture
def gateway():
    return NullGateway(connected=1)


@pytest.fixture
def agent(gateway):
    return build_agent(gateway)


class TestInitialisation:
    def test_builds_with_the_core_tools(self, agent):
        # build_agent now also wires the Spotify tools, using
        # the fake service by default.
        for expected in ["get_bot_status", "get_time", "set_expression"]:
            assert expected in agent.registry.names()

    def test_builds_with_spotify_by_default(self, agent):
        spotify = [n for n in agent.registry.names() if n.startswith("spotify_")]
        assert len(spotify) == 10

    def test_registry_holds_nothing_else(self, agent):
        # A tool appearing here that nobody declared would be
        # a real problem: this is the model's whole surface.
        allowed = {"get_bot_status", "get_time", "set_expression"}
        for name in agent.registry.names():
            assert name in allowed or name.startswith("spotify_")

    def test_conversation_starts_with_system_prompt(self, agent):
        conversation = agent.conversation("c1")
        assert len(conversation.messages) == 1
        assert conversation.messages[0].role is Role.SYSTEM

    def test_provider_is_swappable(self, gateway):
        # The point of the abstraction: no agent change.
        scripted = ScriptedProvider([LLMResponse(text="hi")])
        agent = build_agent(gateway, provider=scripted)
        assert agent.provider is scripted


class TestConversation:
    async def test_history_accumulates_across_turns(self, agent):
        await agent.send("hello there", conversation_id="c1")
        await agent.send("hello again", conversation_id="c1")

        roles = [m.role for m in agent.conversation("c1").messages]
        assert roles.count(Role.USER) == 2
        assert roles.count(Role.ASSISTANT) == 2

    async def test_conversations_are_isolated(self, agent):
        await agent.send("first", conversation_id="a")
        await agent.send("second", conversation_id="b")

        a_text = [m.content for m in agent.conversation("a").messages]
        assert "second" not in a_text

    async def test_reset_keeps_system_prompt_only(self, agent):
        await agent.send("hello", conversation_id="c1")
        assert agent.reset("c1") is True

        messages = agent.conversation("c1").messages
        assert len(messages) == 1
        assert messages[0].role is Role.SYSTEM

    def test_reset_unknown_conversation_is_false(self, agent):
        assert agent.reset("never-existed") is False

    async def test_history_is_trimmed_but_keeps_system(self, agent):
        conversation = agent.conversation("c1")
        conversation.max_messages = 6

        for i in range(10):
            await agent.send(f"message {i}", conversation_id="c1")

        messages = conversation.messages
        assert len(messages) <= 6
        assert messages[0].role is Role.SYSTEM

    async def test_empty_input_rejected(self, agent):
        response = await agent.send("   ", conversation_id="c1")
        assert not response.ok
        assert response.error == "empty input"


class TestToolSelection:
    async def test_time_question_calls_get_time(self, agent):
        response = await agent.send("what time is it?", conversation_id="c1")

        assert response.ok
        assert response.tool_names == ["get_time"]
        assert "It is" in response.text

    async def test_expression_request_calls_set_expression(self, agent, gateway):
        response = await agent.send("look happy", conversation_id="c1")

        assert response.ok
        assert response.tool_names == ["set_expression"]
        assert gateway.sent == [{"type": "expression", "value": "happy"}]

    async def test_status_question_calls_get_bot_status(self, agent):
        response = await agent.send("what is your status?", conversation_id="c1")
        assert response.tool_names == ["get_bot_status"]

    async def test_plain_chat_uses_no_tools(self, agent):
        response = await agent.send("tell me a story", conversation_id="c1")
        assert response.tool_names == []
        assert response.ok


class TestEsp32Integration:
    async def test_expression_reaches_gateway_via_protocol_v1(self, gateway):
        agent = build_agent(gateway)
        await agent.send("be excited", conversation_id="c1")

        # Exactly the Protocol v1 shape -- no second protocol.
        assert gateway.sent == [{"type": "expression", "value": "excited"}]

    async def test_disconnected_bot_does_not_fail_the_turn(self):
        offline = NullGateway(connected=0)
        agent = build_agent(offline)

        response = await agent.send("look happy", conversation_id="c1")

        assert response.ok
        assert "No bot is connected" in response.text

    async def test_agent_survives_gateway_exception(self, monkeypatch):
        gateway = NullGateway(connected=1)

        async def boom(value: str) -> int:
            raise ConnectionError("socket died")

        monkeypatch.setattr(gateway, "send_expression", boom)
        agent = build_agent(gateway)

        response = await agent.send("look happy", conversation_id="c1")

        # The tool failed, but the turn completed.
        assert response.tools_used
        assert not response.tools_used[0].result.ok
        assert "ConnectionError" in response.tools_used[0].result.error


class TestMultiTurn:
    async def test_tool_result_feeds_back_into_the_provider(self, gateway):
        scripted = ScriptedProvider(
            [
                LLMResponse(tool_calls=[ToolCall(name="get_time", arguments={})]),
                LLMResponse(text="It is late."),
            ]
        )
        agent = Agent(provider=scripted, registry=default_registry(gateway))

        response = await agent.send("what time is it?", conversation_id="c1")

        assert response.text == "It is late."
        assert scripted.calls == 2

        # The second call must have seen the tool result.
        second = scripted.seen_messages[1]
        assert second[-1].role is Role.TOOL
        assert second[-1].tool_name == "get_time"

    async def test_several_tools_in_one_turn(self, gateway):
        scripted = ScriptedProvider(
            [
                LLMResponse(
                    tool_calls=[
                        ToolCall(name="get_time", arguments={}),
                        ToolCall(name="get_bot_status", arguments={}),
                    ]
                ),
                LLMResponse(text="Both done."),
            ]
        )
        agent = Agent(provider=scripted, registry=default_registry(gateway))

        response = await agent.send("time and status", conversation_id="c1")

        assert response.tool_names == ["get_time", "get_bot_status"]
        assert response.text == "Both done."


class TestFailureModes:
    async def test_provider_failure_is_graceful(self, gateway):
        agent = Agent(
            provider=FailingProvider("API key rejected"),
            registry=default_registry(gateway),
        )

        response = await agent.send("hello", conversation_id="c1")

        assert not response.ok
        assert "provider error" in response.error
        assert response.text  # still says something usable

    async def test_provider_timeout_is_graceful(self, gateway):
        class SlowProvider(LLMProvider):
            name = "slow"

            async def generate(self, messages, tools):
                await asyncio.sleep(5)
                return LLMResponse(text="too late")

        agent = Agent(
            provider=SlowProvider(),
            registry=default_registry(gateway),
            provider_timeout_s=0.2,
        )

        response = await agent.send("hello", conversation_id="c1")

        assert not response.ok
        assert "timeout" in response.error

    async def test_unexpected_provider_exception_is_caught(self, gateway):
        class BrokenProvider(LLMProvider):
            name = "broken"

            async def generate(self, messages, tools):
                raise ValueError("bad SDK response")

        agent = Agent(
            provider=BrokenProvider(), registry=default_registry(gateway)
        )

        response = await agent.send("hello", conversation_id="c1")

        assert not response.ok
        assert "ValueError" in response.error

    async def test_invented_tool_name_is_reported_not_executed(self, gateway):
        scripted = ScriptedProvider(
            [
                LLMResponse(
                    tool_calls=[ToolCall(name="delete_everything", arguments={})]
                ),
                LLMResponse(text="I could not do that."),
            ]
        )
        agent = Agent(provider=scripted, registry=default_registry(gateway))

        response = await agent.send("do something bad", conversation_id="c1")

        assert not response.tools_used[0].result.ok
        assert "unknown tool" in response.tools_used[0].result.error
        assert gateway.sent == []

    async def test_bad_arguments_reported_back_to_model(self, gateway):
        scripted = ScriptedProvider(
            [
                LLMResponse(
                    tool_calls=[
                        ToolCall(
                            name="set_expression",
                            arguments={"expression": "wizard"},
                        )
                    ]
                ),
                LLMResponse(text="That face does not exist."),
            ]
        )
        agent = Agent(provider=scripted, registry=default_registry(gateway))

        response = await agent.send("look like a wizard", conversation_id="c1")

        assert not response.tools_used[0].result.ok
        assert "must be one of" in response.tools_used[0].result.error
        assert gateway.sent == []

    async def test_empty_provider_response_is_an_error(self, gateway):
        agent = Agent(
            provider=ScriptedProvider([LLMResponse(text="   ")]),
            registry=default_registry(gateway),
        )

        response = await agent.send("hello", conversation_id="c1")

        assert not response.ok
        assert "malformed provider response" in response.error

    async def test_endless_tool_loop_is_capped(self, gateway):
        # A provider that only ever asks for tools.
        class LoopingProvider(LLMProvider):
            name = "looping"

            def __init__(self):
                self.calls = 0

            async def generate(self, messages, tools):
                self.calls += 1
                return LLMResponse(
                    tool_calls=[ToolCall(name="get_time", arguments={})]
                )

        provider = LoopingProvider()
        agent = Agent(
            provider=provider,
            registry=default_registry(gateway),
            max_iterations=3,
        )

        response = await agent.send("loop forever", conversation_id="c1")

        assert not response.ok
        assert "exceeded 3 tool iterations" in response.error
        assert provider.calls == 3


class TestResponseShape:
    async def test_to_dict_is_serialisable(self, agent):
        import json

        response = await agent.send("what time is it?", conversation_id="c1")
        payload = json.dumps(response.to_dict())

        assert "conversation_id" in payload
        assert "tools_used" in payload

    async def test_conversation_id_is_generated_when_absent(self, agent):
        response = await agent.send("hello")
        assert response.conversation_id
