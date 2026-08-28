"""LLM provider abstraction.

The Agent talks to this interface and never to a vendor
SDK. Swapping Anthropic for anything else means writing one
subclass; no agent code changes.

Two providers ship here, both offline:

  FakeLLMProvider   — keyword rules, good enough to drive
                      the whole loop in tests and demos
  ScriptedProvider  — returns a fixed queue of responses,
                      for testing one exact path

A real provider is a Phase E+ concern. When one is added,
its API key comes from the environment and never touches
the ESP32.
"""

from __future__ import annotations

import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any

from .messages import Message, Role


@dataclass
class ToolCall:
    name: str
    arguments: dict[str, Any]
    id: str = field(default_factory=lambda: uuid.uuid4().hex[:8])


@dataclass
class LLMResponse:
    """Either a final answer or a request to use tools."""

    text: str | None = None
    tool_calls: list[ToolCall] = field(default_factory=list)

    @property
    def wants_tools(self) -> bool:
        return bool(self.tool_calls)


class ProviderError(Exception):
    """The provider could not produce a response.

    Network failure, auth failure, rate limit, malformed
    output -- the Agent treats them all the same way.
    """


class LLMProvider(ABC):
    name: str = "abstract"

    @abstractmethod
    async def generate(
        self, messages: list[Message], tools: list[dict]
    ) -> LLMResponse: ...


# ---------------------------------------------------------
# Offline providers
# ---------------------------------------------------------


class FakeLLMProvider(LLMProvider):
    """Deterministic, rule-based, no network.

    Not pretending to be intelligent -- it exists so the
    agent loop, tool dispatch and ESP32 round trip can be
    exercised end to end with no API key and no internet.
    """

    name = "fake"

    # Keyword -> tool. First match wins, so order matters.
    EXPRESSION_WORDS = {
        "happy": "happy",
        "sad": "sleepy",
        "excited": "excited",
        "angry": "angry",
        "confused": "confused",
        "sleepy": "sleepy",
        "thinking": "thinking",
        "love": "love",
    }

    def __init__(self) -> None:
        self.calls = 0

    async def generate(
        self, messages: list[Message], tools: list[dict]
    ) -> LLMResponse:
        self.calls += 1

        available = {spec["name"] for spec in tools}

        # A tool result is in hand -- summarise and stop.
        # Without this the loop would call tools forever.
        last = messages[-1] if messages else None
        if last is not None and last.role is Role.TOOL:
            return LLMResponse(text=self._summarise(last))

        prompt = self._latest_user_text(messages).lower()

        if not prompt:
            return LLMResponse(text="I did not catch that.")

        if "time" in prompt and "get_time" in available:
            return LLMResponse(tool_calls=[ToolCall(name="get_time", arguments={})])

        spotify = self._spotify_intent(prompt, available)
        if spotify is not None:
            return LLMResponse(tool_calls=[spotify])

        if ("status" in prompt or "how are you" in prompt) and (
            "get_bot_status" in available
        ):
            return LLMResponse(
                tool_calls=[ToolCall(name="get_bot_status", arguments={})]
            )

        if "set_expression" in available:
            for word, expression in self.EXPRESSION_WORDS.items():
                if word in prompt:
                    return LLMResponse(
                        tool_calls=[
                            ToolCall(
                                name="set_expression",
                                arguments={"expression": expression},
                            )
                        ]
                    )

        return LLMResponse(text=f"You said: {self._latest_user_text(messages)}")

    @staticmethod
    def _spotify_intent(prompt: str, available: set[str]) -> ToolCall | None:
        """Crude keyword routing for the offline provider.

        Enough to drive the whole Spotify path with no API
        key. A real model does this properly.
        """
        if not any(name.startswith("spotify_") for name in available):
            return None

        def call(name: str, **arguments) -> ToolCall | None:
            return ToolCall(name=name, arguments=arguments) if name in available else None

        # Transport controls first - they are unambiguous.
        if "pause" in prompt:
            return call("spotify_pause")
        if "skip" in prompt or "next track" in prompt or "next song" in prompt:
            return call("spotify_next")
        if "previous" in prompt or "go back" in prompt:
            return call("spotify_previous")
        if "resume" in prompt or "unpause" in prompt:
            return call("spotify_resume")

        if any(
            phrase in prompt
            for phrase in ("what's playing", "whats playing", "what song",
                           "now playing", "who made this", "what is playing")
        ):
            return call("spotify_now_playing")

        if "play" not in prompt:
            return None

        # Everything after the word "play" is the subject.
        subject = prompt.split("play", 1)[1].strip(" .!?,")

        for filler in ("me ", "my ", "some ", "something from ", "the "):
            if subject.startswith(filler):
                subject = subject[len(filler):].strip()

        if "playlist" in prompt:
            subject = subject.replace("playlist", "").strip() or "coding"
            return call("spotify_play_playlist", query=subject)

        if "album" in prompt:
            subject = subject.replace("album", "").strip()
            return call("spotify_play_album", query=subject)

        if not subject or subject in ("spotify", "music"):
            return call("spotify_play")

        return call("spotify_play_track", query=subject)

    @staticmethod
    def _latest_user_text(messages: list[Message]) -> str:
        for message in reversed(messages):
            if message.role is Role.USER:
                return message.content
        return ""

    @staticmethod
    def _summarise(result: Message) -> str:
        if result.tool_name == "get_time":
            return f"It is {result.content}."
        if result.tool_name == "set_expression":
            return result.content
        if result.tool_name == "get_bot_status":
            return f"Status: {result.content}"
        return result.content


class ScriptedProvider(LLMProvider):
    """Replays a fixed list of responses, in order.

    Used to drive one specific path through the agent --
    including paths the rule-based fake would never take,
    like a tool call with bad arguments.
    """

    name = "scripted"

    def __init__(self, responses: list[LLMResponse]) -> None:
        self._responses = list(responses)
        self.calls = 0
        self.seen_messages: list[list[Message]] = []

    async def generate(
        self, messages: list[Message], tools: list[dict]
    ) -> LLMResponse:
        self.calls += 1
        self.seen_messages.append(list(messages))

        if not self._responses:
            raise ProviderError("scripted provider ran out of responses")

        return self._responses.pop(0)


class FailingProvider(LLMProvider):
    """Always raises. For testing provider failure."""

    name = "failing"

    def __init__(self, message: str = "provider unavailable") -> None:
        self.message = message

    async def generate(
        self, messages: list[Message], tools: list[dict]
    ) -> LLMResponse:
        raise ProviderError(self.message)
