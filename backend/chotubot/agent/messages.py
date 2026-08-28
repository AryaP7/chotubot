"""Conversation state.

Short-term only, as specified. The store is an interface
with an in-memory implementation so Phase F can add a
persistent one without touching the Agent.
"""

from __future__ import annotations

import time
import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum


class Role(str, Enum):
    SYSTEM = "system"
    USER = "user"
    ASSISTANT = "assistant"
    TOOL = "tool"


@dataclass
class Message:
    role: Role
    content: str

    # Set only on TOOL messages, tying a result back to the
    # call that produced it.
    tool_name: str | None = None
    tool_call_id: str | None = None

    # Set only on ASSISTANT messages that requested tools.
    # Each entry is {"id", "name", "arguments"}.
    #
    # Required for a real provider: the Messages API expects
    # every tool_result to follow an assistant turn holding
    # the matching tool_use block with the same id. Without
    # this the history is malformed and the API rejects it.
    tool_calls: list[dict] | None = None

    created_at: float = field(default_factory=time.time)

    def to_dict(self) -> dict:
        payload = {"role": self.role.value, "content": self.content}
        if self.tool_name:
            payload["tool_name"] = self.tool_name
        if self.tool_call_id:
            payload["tool_call_id"] = self.tool_call_id
        if self.tool_calls:
            payload["tool_calls"] = self.tool_calls
        return payload


@dataclass
class Conversation:
    id: str
    messages: list[Message] = field(default_factory=list)
    created_at: float = field(default_factory=time.time)

    # Kept bounded because context windows are finite and
    # this bot runs for days at a time. The system prompt
    # is never trimmed.
    max_messages: int = 40

    def append(self, message: Message) -> None:
        self.messages.append(message)
        self._trim()

    def history(self) -> list[Message]:
        return list(self.messages)

    def reset(self, keep_system: bool = True) -> None:
        if keep_system:
            self.messages = [m for m in self.messages if m.role is Role.SYSTEM]
        else:
            self.messages = []

    def _trim(self) -> None:
        if len(self.messages) <= self.max_messages:
            return

        system = [m for m in self.messages if m.role is Role.SYSTEM]
        rest = [m for m in self.messages if m.role is not Role.SYSTEM]

        keep = self.max_messages - len(system)
        self.messages = system + rest[-keep:] if keep > 0 else system


class ConversationStore(ABC):
    """Where conversations live.

    Phase F swaps the implementation, not the interface.
    """

    @abstractmethod
    def get_or_create(self, conversation_id: str | None = None) -> Conversation: ...

    @abstractmethod
    def get(self, conversation_id: str) -> Conversation | None: ...

    @abstractmethod
    def delete(self, conversation_id: str) -> bool: ...

    @abstractmethod
    def list_ids(self) -> list[str]: ...


class InMemoryConversationStore(ConversationStore):
    def __init__(self) -> None:
        self._conversations: dict[str, Conversation] = {}

    def get_or_create(self, conversation_id: str | None = None) -> Conversation:
        if conversation_id is None:
            conversation_id = uuid.uuid4().hex[:12]

        if conversation_id not in self._conversations:
            self._conversations[conversation_id] = Conversation(id=conversation_id)

        return self._conversations[conversation_id]

    def get(self, conversation_id: str) -> Conversation | None:
        return self._conversations.get(conversation_id)

    def delete(self, conversation_id: str) -> bool:
        return self._conversations.pop(conversation_id, None) is not None

    def list_ids(self) -> list[str]:
        return list(self._conversations)
