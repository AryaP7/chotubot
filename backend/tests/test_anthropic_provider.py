"""AnthropicProvider, offline.

Every test here injects a stub client. Nothing reaches the
network and no API key is needed. The live test lives in
test_live_llm.py and is skipped without credentials.
"""

from __future__ import annotations

import pytest

from chotubot import protocol
from chotubot.agent import (
    Agent,
    Message,
    NullGateway,
    ProviderError,
    Role,
    default_registry,
)
from chotubot.agent.anthropic_provider import AnthropicProvider, _redact


# --- stub SDK objects ------------------------------------


class Block:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class StubResponse:
    def __init__(self, content, stop_reason="end_turn", stop_details=None):
        self.content = content
        self.stop_reason = stop_reason
        self.stop_details = stop_details


class StubMessages:
    def __init__(self, responses=None, error=None):
        self._responses = list(responses or [])
        self._error = error
        self.requests: list[dict] = []

    async def create(self, **kwargs):
        self.requests.append(kwargs)
        if self._error is not None:
            raise self._error
        if not self._responses:
            raise AssertionError("stub ran out of responses")
        return self._responses.pop(0)


class StubClient:
    def __init__(self, responses=None, error=None):
        self.messages = StubMessages(responses, error)
        self.beta = self

    @property
    def requests(self):
        return self.messages.requests


def provider(responses=None, error=None, **kwargs):
    return AnthropicProvider(client=StubClient(responses, error), **kwargs)


def text_block(text):
    return Block(type="text", text=text)


def tool_block(id, name, input):
    return Block(type="tool_use", id=id, name=name, input=input)


# --- tests -----------------------------------------------


class TestConfiguration:
    def test_no_api_key_is_a_clean_error(self, monkeypatch):
        monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
        monkeypatch.delenv("LLM_API_KEY", raising=False)

        with pytest.raises(ProviderError, match="No API key"):
            AnthropicProvider()

    def test_defaults_from_environment(self, monkeypatch):
        monkeypatch.setenv("LLM_MODEL", "claude-sonnet-5")
        monkeypatch.setenv("LLM_MAX_TOKENS", "2048")
        monkeypatch.setenv("LLM_EFFORT", "low")

        p = provider()
        assert p.model == "claude-sonnet-5"
        assert p.max_tokens == 2048
        assert p.effort == "low"

    def test_unknown_provider_name_rejected(self):
        from chotubot.agent import make_provider

        with pytest.raises(ProviderError, match="unknown LLM_PROVIDER"):
            make_provider("gpt")

    def test_factory_defaults_to_fake(self, monkeypatch):
        from chotubot.agent import make_provider

        monkeypatch.delenv("LLM_PROVIDER", raising=False)
        assert make_provider().name == "fake"


class TestRequestTranslation:
    async def test_system_prompt_is_lifted_out_of_messages(self):
        p = provider([StubResponse([text_block("hi")])])

        await p.generate(
            [
                Message(role=Role.SYSTEM, content="You are Chotubot."),
                Message(role=Role.USER, content="hello"),
            ],
            [],
        )

        request = p._client.requests[0]
        assert request["system"] == "You are Chotubot."
        assert request["messages"] == [{"role": "user", "content": "hello"}]

    async def test_tool_spec_becomes_json_schema(self):
        p = provider([StubResponse([text_block("hi")])])
        registry = default_registry(NullGateway())

        await p.generate(
            [Message(role=Role.USER, content="hello")], registry.specs()
        )

        tools = p._client.requests[0]["tools"]
        by_name = {t["name"]: t for t in tools}

        schema = by_name["set_expression"]["input_schema"]
        assert schema["type"] == "object"
        assert schema["required"] == ["expression"]
        assert set(schema["properties"]["expression"]["enum"]) == set(
            protocol.EXPRESSIONS
        )

    async def test_tool_call_and_result_are_paired(self):
        """A tool_result must follow the matching tool_use."""
        p = provider([StubResponse([text_block("done")])])

        await p.generate(
            [
                Message(role=Role.USER, content="what time is it?"),
                Message(
                    role=Role.ASSISTANT,
                    content="",
                    tool_calls=[
                        {"id": "tu_1", "name": "get_time", "arguments": {}}
                    ],
                ),
                Message(
                    role=Role.TOOL,
                    content="12:00",
                    tool_name="get_time",
                    tool_call_id="tu_1",
                ),
            ],
            [],
        )

        messages = p._client.requests[0]["messages"]

        assistant = messages[1]
        assert assistant["role"] == "assistant"
        assert assistant["content"][0]["type"] == "tool_use"
        assert assistant["content"][0]["id"] == "tu_1"

        result = messages[2]
        assert result["role"] == "user"
        assert result["content"][0]["tool_use_id"] == "tu_1"

    async def test_parallel_results_merge_into_one_message(self):
        """Splitting them teaches the model to stop calling
        tools in parallel."""
        p = provider([StubResponse([text_block("ok")])])

        await p.generate(
            [
                Message(role=Role.USER, content="time and status"),
                Message(
                    role=Role.ASSISTANT,
                    content="",
                    tool_calls=[
                        {"id": "a", "name": "get_time", "arguments": {}},
                        {"id": "b", "name": "get_bot_status", "arguments": {}},
                    ],
                ),
                Message(role=Role.TOOL, content="12:00", tool_call_id="a"),
                Message(role=Role.TOOL, content="offline", tool_call_id="b"),
            ],
            [],
        )

        messages = p._client.requests[0]["messages"]
        results = [m for m in messages if m["role"] == "user" and isinstance(
            m["content"], list)]

        assert len(results) == 1
        assert len(results[0]["content"]) == 2

    async def test_empty_history_is_rejected(self):
        p = provider([StubResponse([text_block("hi")])])

        with pytest.raises(ProviderError, match="no user turn"):
            await p.generate([Message(role=Role.SYSTEM, content="sys")], [])

    async def test_request_includes_effort_and_fallbacks(self):
        p = provider([StubResponse([text_block("hi")])])
        await p.generate([Message(role=Role.USER, content="hi")], [])

        request = p._client.requests[0]
        assert request["output_config"]["effort"] == "medium"
        assert request["fallbacks"] == "default"
        assert "server-side-fallback-2026-07-01" in request["betas"]


class TestResponseParsing:
    async def test_plain_text_response(self):
        p = provider([StubResponse([text_block("Hello there.")])])
        result = await p.generate([Message(role=Role.USER, content="hi")], [])

        assert result.text == "Hello there."
        assert not result.wants_tools

    async def test_tool_call_response(self):
        p = provider(
            [
                StubResponse(
                    [tool_block("tu_1", "set_expression", {"expression": "happy"})],
                    stop_reason="tool_use",
                )
            ]
        )
        result = await p.generate([Message(role=Role.USER, content="be happy")], [])

        assert result.wants_tools
        assert result.tool_calls[0].name == "set_expression"
        assert result.tool_calls[0].arguments == {"expression": "happy"}
        assert result.tool_calls[0].id == "tu_1"

    async def test_text_and_tool_call_together(self):
        p = provider(
            [
                StubResponse(
                    [
                        text_block("Let me check."),
                        tool_block("tu_1", "get_time", {}),
                    ]
                )
            ]
        )
        result = await p.generate([Message(role=Role.USER, content="time?")], [])

        assert result.text == "Let me check."
        assert result.tool_calls[0].name == "get_time"

    async def test_refusal_becomes_provider_error(self):
        p = provider(
            [
                StubResponse(
                    [text_block("...")],
                    stop_reason="refusal",
                    stop_details=Block(type="refusal", category="cyber"),
                )
            ]
        )

        with pytest.raises(ProviderError, match="declined"):
            await p.generate([Message(role=Role.USER, content="bad")], [])

    async def test_empty_content_is_an_error(self):
        p = provider([StubResponse([])])

        with pytest.raises(ProviderError, match="no content blocks"):
            await p.generate([Message(role=Role.USER, content="hi")], [])

    async def test_thinking_blocks_are_ignored(self):
        p = provider(
            [
                StubResponse(
                    [Block(type="thinking", thinking=""), text_block("Answer.")]
                )
            ]
        )
        result = await p.generate([Message(role=Role.USER, content="hi")], [])
        assert result.text == "Answer."


class TestErrorTranslation:
    async def test_network_failure(self):
        import anthropic

        error = anthropic.APIConnectionError(request=None)
        p = provider(error=error)

        with pytest.raises(ProviderError, match="could not reach the API"):
            await p.generate([Message(role=Role.USER, content="hi")], [])

    async def test_unexpected_exception_still_becomes_provider_error(self):
        p = provider(error=ValueError("something odd"))

        with pytest.raises(ProviderError, match="ValueError"):
            await p.generate([Message(role=Role.USER, content="hi")], [])

    def test_keys_are_redacted_from_error_text(self):
        leaked = "auth failed for sk-ant-api03-ABCdef123456789xyz"
        cleaned = _redact(leaked)

        assert "sk-ant-api03-ABCdef123456789xyz" not in cleaned
        assert "REDACTED" in cleaned


class TestAgentWithRealProviderShape:
    """The Agent must not care which provider it holds."""

    async def test_full_loop_through_the_anthropic_provider(self):
        gateway = NullGateway(connected=1)

        p = provider(
            [
                StubResponse(
                    [tool_block("tu_1", "set_expression", {"expression": "happy"})],
                    stop_reason="tool_use",
                ),
                StubResponse([text_block("Smiling now.")]),
            ]
        )

        agent = Agent(provider=p, registry=default_registry(gateway))
        response = await agent.send("look happy", conversation_id="c1")

        assert response.ok
        assert response.text == "Smiling now."
        assert response.tool_names == ["set_expression"]
        # Reached the bot over Protocol v1.
        assert gateway.sent == [{"type": "expression", "value": "happy"}]

    async def test_provider_failure_does_not_crash_the_agent(self):
        import anthropic

        p = provider(error=anthropic.APIConnectionError(request=None))
        agent = Agent(provider=p, registry=default_registry(NullGateway()))

        response = await agent.send("hello", conversation_id="c1")

        assert not response.ok
        assert "could not reach the API" in response.error
        assert response.text
