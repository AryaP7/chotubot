"""The agent's view of the physical bot.

Tools talk to this, never to `server.hub` directly. Three
reasons: it keeps a circular import out of the picture, it
lets tests run the whole agent with no sockets at all, and
it puts every outbound message through Protocol v1's own
builders rather than hand-rolled dicts.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any

from .. import protocol


@dataclass
class BotStatus:
    connected: int
    bots: list[dict[str, Any]] = field(default_factory=list)

    @property
    def online(self) -> bool:
        return self.connected > 0

    def describe(self) -> str:
        if not self.online:
            return "No bot is connected."

        parts = [
            f"{b.get('fw', 'unknown')} (protocol v{b.get('v', '?')})"
            for b in self.bots
        ]
        return f"{self.connected} bot(s) connected: {', '.join(parts)}"


class BotGateway(ABC):
    """Outbound path to whatever bots are connected."""

    @abstractmethod
    async def send_expression(self, value: str) -> int: ...

    @abstractmethod
    async def send_state(self, name: str) -> int: ...

    @abstractmethod
    async def send_notification(self, title: str, body: str = "") -> int: ...

    @abstractmethod
    async def send_spotify_state(self, **fields: Any) -> int: ...

    @abstractmethod
    def status(self) -> BotStatus: ...


class HubGateway(BotGateway):
    """Real gateway, backed by the server's session hub."""

    def __init__(self, hub: Any) -> None:
        self._hub = hub

    async def send_expression(self, value: str) -> int:
        # Raises ProtocolError for an unknown expression, so
        # an invalid one never reaches the wire.
        return await self._hub.broadcast(protocol.expression(value))

    async def send_state(self, name: str) -> int:
        return await self._hub.broadcast(protocol.state(name))

    async def send_notification(self, title: str, body: str = "") -> int:
        return await self._hub.broadcast(protocol.notification(title, body))

    async def send_spotify_state(self, **fields: Any) -> int:
        # Built through the Protocol v1 constructor, which
        # truncates to the firmware's buffer sizes.
        return await self._hub.broadcast(protocol.spotify_state(**fields))

    def status(self) -> BotStatus:
        sessions = [s for s in self._hub.sessions if s.authenticated]
        return BotStatus(
            connected=len(sessions),
            bots=[
                {"fw": s.firmware, "v": s.version, "peer": s.peer} for s in sessions
            ],
        )


class NullGateway(BotGateway):
    """No bot attached.

    Records what would have been sent and reports zero
    recipients. This is what the agent sees when the ESP32
    is unplugged -- which, right now, is always.
    """

    def __init__(self, connected: int = 0) -> None:
        self.sent: list[dict[str, Any]] = []
        self._connected = connected

    async def send_expression(self, value: str) -> int:
        self.sent.append(protocol.expression(value))
        return self._connected

    async def send_state(self, name: str) -> int:
        self.sent.append(protocol.state(name))
        return self._connected

    async def send_notification(self, title: str, body: str = "") -> int:
        self.sent.append(protocol.notification(title, body))
        return self._connected

    async def send_spotify_state(self, **fields: Any) -> int:
        self.sent.append(protocol.spotify_state(**fields))
        return self._connected

    def status(self) -> BotStatus:
        bots = [
            {"fw": "simulated", "v": protocol.PROTOCOL_VERSION, "peer": "test"}
            for _ in range(self._connected)
        ]
        return BotStatus(connected=self._connected, bots=bots)
