"""The agent loop.

    user text
        -> provider
        -> tool calls?  -> registry -> results -> provider
        -> final text

Everything that can fail is caught and turned into a
response. The agent runs on a machine that is talking to a
physical object over a flaky link; a traceback that kills
the backend is never the right answer.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass, field

from .gateway import BotGateway
from .messages import (
    Conversation,
    ConversationStore,
    InMemoryConversationStore,
    Message,
    Role,
)
from .providers import LLMProvider, ProviderError, ToolCall
from .tools import ToolRegistry, ToolResult

log = logging.getLogger("chotubot.agent")

DEFAULT_SYSTEM_PROMPT = (
    "You are Chotubot, a small desktop companion. You have a face on a "
    "128x64 display and you can change it with the set_expression tool. "
    "Keep replies short -- a sentence or two. Use tools when they would "
    "answer the question better than guessing."
)


@dataclass
class ExecutedTool:
    name: str
    arguments: dict
    result: ToolResult


@dataclass
class AgentResponse:
    """What one turn produced."""

    text: str
    conversation_id: str
    ok: bool = True
    error: str | None = None
    tools_used: list[ExecutedTool] = field(default_factory=list)

    @property
    def tool_names(self) -> list[str]:
        return [t.name for t in self.tools_used]

    def to_dict(self) -> dict:
        return {
            "text": self.text,
            "conversation_id": self.conversation_id,
            "ok": self.ok,
            "error": self.error,
            "tools_used": [
                {
                    "name": t.name,
                    "arguments": t.arguments,
                    "ok": t.result.ok,
                    "content": t.result.content,
                }
                for t in self.tools_used
            ],
        }


class Agent:
    def __init__(
        self,
        provider: LLMProvider,
        registry: ToolRegistry,
        *,
        store: ConversationStore | None = None,
        system_prompt: str = DEFAULT_SYSTEM_PROMPT,
        max_iterations: int = 4,
        provider_timeout_s: float = 30.0,
    ) -> None:
        self.provider = provider
        self.registry = registry
        self.store = store or InMemoryConversationStore()
        self.system_prompt = system_prompt

        # Caps a model that keeps asking for tools instead of
        # answering. Without it, a confused provider spins
        # forever.
        self.max_iterations = max_iterations
        self.provider_timeout_s = provider_timeout_s

    # -- conversation management --------------------------

    def conversation(self, conversation_id: str | None = None) -> Conversation:
        conversation = self.store.get_or_create(conversation_id)

        if not conversation.messages:
            conversation.append(
                Message(role=Role.SYSTEM, content=self.system_prompt)
            )

        return conversation

    def reset(self, conversation_id: str) -> bool:
        conversation = self.store.get(conversation_id)
        if conversation is None:
            return False
        conversation.reset(keep_system=True)
        return True

    # -- the loop -----------------------------------------

    async def send(
        self, user_text: str, conversation_id: str | None = None
    ) -> AgentResponse:
        conversation = self.conversation(conversation_id)

        if not user_text or not user_text.strip():
            return AgentResponse(
                text="I did not receive anything to respond to.",
                conversation_id=conversation.id,
                ok=False,
                error="empty input",
            )

        conversation.append(Message(role=Role.USER, content=user_text.strip()))

        executed: list[ExecutedTool] = []
        specs = self.registry.specs()

        for _ in range(self.max_iterations):
            try:
                response = await asyncio.wait_for(
                    self.provider.generate(conversation.history(), specs),
                    timeout=self.provider_timeout_s,
                )
            except TimeoutError:
                return self._failure(
                    conversation,
                    executed,
                    "The assistant took too long to respond.",
                    f"provider timeout after {self.provider_timeout_s}s",
                )
            except ProviderError as exc:
                return self._failure(
                    conversation,
                    executed,
                    "I could not reach the assistant just now.",
                    f"provider error: {exc}",
                )
            except Exception as exc:  # noqa: BLE001
                # A provider raising something unexpected is a
                # bug in that provider, not a reason to take
                # the backend down.
                log.exception("provider %s raised", self.provider.name)
                return self._failure(
                    conversation,
                    executed,
                    "Something went wrong inside the assistant.",
                    f"{type(exc).__name__}: {exc}",
                )

            if response is None:
                return self._failure(
                    conversation,
                    executed,
                    "The assistant returned nothing.",
                    "malformed provider response: None",
                )

            if not response.wants_tools:
                text = (response.text or "").strip()
                if not text:
                    return self._failure(
                        conversation,
                        executed,
                        "The assistant returned an empty reply.",
                        "malformed provider response: no text and no tool calls",
                    )

                conversation.append(Message(role=Role.ASSISTANT, content=text))
                return AgentResponse(
                    text=text,
                    conversation_id=conversation.id,
                    tools_used=executed,
                )

            # Record the assistant turn that asked for the
            # tools, before their results. A real provider
            # needs this to pair tool_result with tool_use.
            conversation.append(
                Message(
                    role=Role.ASSISTANT,
                    content=(response.text or "").strip(),
                    tool_calls=[
                        {
                            "id": call.id,
                            "name": call.name,
                            "arguments": dict(call.arguments),
                        }
                        for call in response.tool_calls
                    ],
                )
            )

            for call in response.tool_calls:
                executed.append(await self._run_tool(conversation, call))

        # Ran out of iterations still asking for tools.
        return self._failure(
            conversation,
            executed,
            "I got stuck working that out.",
            f"exceeded {self.max_iterations} tool iterations",
        )

    async def _run_tool(
        self, conversation: Conversation, call: ToolCall
    ) -> ExecutedTool:
        # execute() validates first and never raises, so an
        # invented tool name or bad argument comes back as a
        # result the model can read and react to.
        result = await self.registry.execute(call.name, call.arguments)

        if not result.ok:
            log.info("tool %s failed: %s", call.name, result.error)

        conversation.append(
            Message(
                role=Role.TOOL,
                content=result.content,
                tool_name=call.name,
                tool_call_id=call.id,
            )
        )

        return ExecutedTool(
            name=call.name, arguments=dict(call.arguments), result=result
        )

    def _failure(
        self,
        conversation: Conversation,
        executed: list[ExecutedTool],
        text: str,
        error: str,
    ) -> AgentResponse:
        log.warning("agent turn failed: %s", error)
        conversation.append(Message(role=Role.ASSISTANT, content=text))

        return AgentResponse(
            text=text,
            conversation_id=conversation.id,
            ok=False,
            error=error,
            tools_used=executed,
        )


def build_agent(
    gateway: BotGateway,
    provider: LLMProvider | None = None,
    spotify=None,
) -> Agent:
    """Standard wiring.

    With no provider passed, LLM_PROVIDER decides -- and it
    defaults to the offline fake, so nothing starts calling
    a paid API by accident. Likewise SPOTIFY_SERVICE
    defaults to the fake.
    """
    from ..spotify.web_api import make_spotify_service
    from .builtin_tools import default_registry
    from .factory import make_provider

    if spotify is None:
        spotify = make_spotify_service()

    return Agent(
        provider=provider or make_provider(),
        registry=default_registry(gateway, spotify),
    )
