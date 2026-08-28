"""Real LLM provider, backed by the Anthropic Messages API.

This is the ONLY module in the project that imports a
vendor SDK. The Agent, ToolRegistry and BotGateway know
nothing about it -- swapping in a different vendor means
writing another LLMProvider subclass beside this one and
changing one environment variable.

Why the SDK and not raw HTTP: the SDK's transport is
httpx2, which raw HTTP would need anyway, and it brings
retries with backoff, a typed exception per status code,
and content-block parsing. Hand-rolling those is more
code and more ways to be subtly wrong, not fewer.

Credentials come from the environment only. Nothing here
logs, returns, or forwards a key -- see _redact().
"""

from __future__ import annotations

import logging
import os
from typing import Any

from .messages import Message, Role
from .providers import LLMProvider, LLMResponse, ProviderError, ToolCall

log = logging.getLogger("chotubot.agent.anthropic")

# Defaults, all overridable by environment.
DEFAULT_MODEL = "claude-opus-5"

# A desk companion answers in a sentence or two, so a small
# cap keeps replies snappy and cheap. Raise LLM_MAX_TOKENS
# if you give it something long to do.
DEFAULT_MAX_TOKENS = 1024

# Thinking is on by default on this model; effort controls
# how much. "medium" suits short conversational turns with
# three trivial tools.
DEFAULT_EFFORT = "medium"


def _redact(text: str) -> str:
    """Strip anything key-shaped out of a message.

    Belt and braces: SDK errors should never contain a key,
    but this runs on every error string that reaches a log,
    a WebSocket, or a test report.
    """
    import re

    return re.sub(r"sk-[A-Za-z0-9_\-]{8,}", "sk-***REDACTED***", text)


class AnthropicProvider(LLMProvider):
    name = "anthropic"

    def __init__(
        self,
        *,
        model: str | None = None,
        max_tokens: int | None = None,
        effort: str | None = None,
        api_key: str | None = None,
        client: Any = None,
        timeout_s: float = 30.0,
    ) -> None:
        self.model = model or os.environ.get("LLM_MODEL", DEFAULT_MODEL)
        self.max_tokens = max_tokens or int(
            os.environ.get("LLM_MAX_TOKENS", DEFAULT_MAX_TOKENS)
        )
        self.effort = effort or os.environ.get("LLM_EFFORT", DEFAULT_EFFORT)
        self.timeout_s = timeout_s

        if client is not None:
            # Injected for tests. No network, no credentials.
            self._client = client
            return

        # ANTHROPIC_API_KEY is what the SDK reads on its own;
        # LLM_API_KEY is accepted as the project-generic
        # alias. Neither is ever written down anywhere.
        key = (
            api_key
            or os.environ.get("ANTHROPIC_API_KEY")
            or os.environ.get("LLM_API_KEY")
        )

        if not key:
            raise ProviderError(
                "No API key. Set ANTHROPIC_API_KEY (or LLM_API_KEY) in the "
                "environment, or use LLM_PROVIDER=fake for offline work."
            )

        try:
            import anthropic
        except ImportError as exc:  # pragma: no cover - install-time only
            raise ProviderError(
                "The 'anthropic' package is not installed. "
                "pip install -r requirements.txt"
            ) from exc

        self._client = anthropic.AsyncAnthropic(api_key=key, timeout=timeout_s)

    # -- translation: ours -> Anthropic --------------------

    @staticmethod
    def _to_input_schema(spec: dict) -> dict:
        """ToolRegistry's generic spec -> JSON Schema."""
        properties: dict[str, Any] = {}
        required: list[str] = []

        for param in spec.get("parameters", []):
            prop: dict[str, Any] = {
                "type": param.get("type", "string"),
                "description": param.get("description", ""),
            }
            if param.get("enum"):
                prop["enum"] = param["enum"]

            properties[param["name"]] = prop
            if param.get("required", True):
                required.append(param["name"])

        return {
            "type": "object",
            "properties": properties,
            "required": required,
        }

    def _to_tools(self, specs: list[dict]) -> list[dict]:
        return [
            {
                "name": spec["name"],
                "description": spec["description"],
                "input_schema": self._to_input_schema(spec),
            }
            for spec in specs
        ]

    @staticmethod
    def _to_messages(history: list[Message]) -> tuple[str, list[dict]]:
        """Split our flat history into (system, messages).

        Two rules the API enforces that our flat list does
        not: system prompts live outside `messages`, and
        consecutive tool results must be merged into ONE
        user message. Splitting parallel results across
        several messages teaches the model to stop making
        parallel calls.
        """
        system_parts: list[str] = []
        messages: list[dict] = []
        pending_results: list[dict] = []

        def flush_results() -> None:
            nonlocal pending_results
            if pending_results:
                messages.append({"role": "user", "content": pending_results})
                pending_results = []

        for message in history:
            if message.role is Role.SYSTEM:
                system_parts.append(message.content)
                continue

            if message.role is Role.TOOL:
                pending_results.append(
                    {
                        "type": "tool_result",
                        "tool_use_id": message.tool_call_id or "unknown",
                        "content": message.content,
                    }
                )
                continue

            flush_results()

            if message.role is Role.USER:
                messages.append({"role": "user", "content": message.content})
                continue

            # Assistant, with or without tool calls.
            blocks: list[dict] = []
            if message.content:
                blocks.append({"type": "text", "text": message.content})

            for call in message.tool_calls or []:
                blocks.append(
                    {
                        "type": "tool_use",
                        "id": call["id"],
                        "name": call["name"],
                        "input": call["arguments"],
                    }
                )

            if blocks:
                messages.append({"role": "assistant", "content": blocks})

        flush_results()
        return "\n\n".join(system_parts), messages

    # -- the call -----------------------------------------

    async def generate(
        self, messages: list[Message], tools: list[dict]
    ) -> LLMResponse:
        system, api_messages = self._to_messages(messages)

        if not api_messages:
            raise ProviderError("nothing to send: history has no user turn")

        request: dict[str, Any] = {
            "model": self.model,
            "max_tokens": self.max_tokens,
            "messages": api_messages,
            "output_config": {"effort": self.effort},
            # Server-side fallback: if a safety classifier
            # declines, the request is rerouted instead of
            # coming back as a dead end.
            "betas": ["server-side-fallback-2026-07-01"],
            "fallbacks": "default",
        }

        if system:
            request["system"] = system
        if tools:
            request["tools"] = self._to_tools(tools)

        try:
            response = await self._client.beta.messages.create(**request)
        except Exception as exc:  # noqa: BLE001
            raise self._translate_error(exc) from exc

        return self._parse(response)

    @staticmethod
    def _translate_error(exc: Exception) -> ProviderError:
        """Map SDK exceptions onto one clean ProviderError.

        Caught most-specific-first. Every branch produces a
        message the operator can act on, with anything
        key-shaped stripped out.
        """
        try:
            import anthropic
        except ImportError:  # pragma: no cover
            return ProviderError(_redact(f"{type(exc).__name__}: {exc}"))

        if isinstance(exc, anthropic.AuthenticationError):
            return ProviderError("authentication failed: the API key was rejected")
        if isinstance(exc, anthropic.NotFoundError):
            return ProviderError(f"model not found: {_redact(str(exc))}")
        if isinstance(exc, anthropic.RateLimitError):
            return ProviderError("rate limited by the API; back off and retry")
        if isinstance(exc, anthropic.APITimeoutError):
            return ProviderError("the API request timed out")
        if isinstance(exc, anthropic.APIConnectionError):
            return ProviderError("could not reach the API (network failure)")
        if isinstance(exc, anthropic.APIStatusError):
            return ProviderError(
                f"API returned {exc.status_code}: {_redact(str(exc.message))}"
            )

        return ProviderError(_redact(f"{type(exc).__name__}: {exc}"))

    @staticmethod
    def _parse(response: Any) -> LLMResponse:
        stop_reason = getattr(response, "stop_reason", None)

        # Check before touching content: on a refusal the
        # content blocks are not a usable answer.
        if stop_reason == "refusal":
            details = getattr(response, "stop_details", None)
            category = getattr(details, "category", None) or "unspecified"
            raise ProviderError(f"the model declined this request ({category})")

        content = getattr(response, "content", None)
        if not content:
            raise ProviderError("the API returned no content blocks")

        text_parts: list[str] = []
        tool_calls: list[ToolCall] = []

        for block in content:
            block_type = getattr(block, "type", None)

            if block_type == "text":
                text_parts.append(block.text)
            elif block_type == "tool_use":
                # block.input is already parsed by the SDK;
                # never string-match the serialised form.
                tool_calls.append(
                    ToolCall(
                        id=block.id,
                        name=block.name,
                        arguments=dict(block.input or {}),
                    )
                )
            # thinking blocks are ignored -- see the known
            # limitation in STATUS.md.

        text = "\n".join(p for p in text_parts if p).strip()

        if not text and not tool_calls:
            raise ProviderError("the API returned neither text nor a tool call")

        return LLMResponse(text=text or None, tool_calls=tool_calls)
