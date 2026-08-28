"""The starter tool set.

Three tools, all safe, all real. They exist to prove the
loop works end to end -- not to be impressive.

Nothing here shells out, opens a socket, or touches the
filesystem. Later phases add capability by declaring more
tools like these, never by loosening the registry.
"""

from __future__ import annotations

from datetime import datetime

from .. import protocol
from ..protocol import ProtocolError
from .gateway import BotGateway
from .tools import Tool, ToolParameter, ToolResult


class GetTimeTool(Tool):
    name = "get_time"
    description = "Get the current date and time on the backend machine."
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        now = datetime.now()
        return ToolResult.success(now.strftime("%A %d %B %Y, %H:%M:%S"))


class GetBotStatusTool(Tool):
    name = "get_bot_status"
    description = (
        "Check whether the physical bot is connected and which "
        "firmware it is running."
    )
    parameters: list[ToolParameter] = []

    def __init__(self, gateway: BotGateway) -> None:
        self._gateway = gateway

    async def run(self) -> ToolResult:
        return ToolResult.success(self._gateway.status().describe())


class SetExpressionTool(Tool):
    name = "set_expression"
    description = (
        "Change the face shown on the bot's display. Use this to react "
        "visibly to the conversation."
    )

    # The enum is the allowlist the registry validates
    # against, taken from the protocol rather than restated,
    # so the two cannot drift apart.
    parameters = [
        ToolParameter(
            name="expression",
            description="Which face to show.",
            enum=sorted(protocol.EXPRESSIONS),
        )
    ]

    def __init__(self, gateway: BotGateway) -> None:
        self._gateway = gateway

    async def run(self, expression: str) -> ToolResult:
        try:
            delivered = await self._gateway.send_expression(expression)
        except ProtocolError as exc:
            # Belt and braces: the registry already checked
            # the enum, so reaching here means the allowlist
            # and the protocol disagree.
            return ToolResult.failure(str(exc))

        if delivered == 0:
            # Not an error. The agent should know the bot did
            # not see it, and say so, rather than claiming
            # something happened that did not.
            return ToolResult.success(
                f"No bot is connected, so the {expression} expression "
                f"was not displayed."
            )

        return ToolResult.success(
            f"Set the expression to {expression} on {delivered} bot(s)."
        )


def default_registry(gateway: BotGateway, spotify=None):
    """Build the standard registry.

    Spotify is opt-in: pass a service to add its ten tools.
    With none, the model simply has no way to reach Spotify
    rather than having tools that fail.
    """
    from .tools import ToolRegistry

    registry = ToolRegistry()
    registry.register(GetTimeTool())
    registry.register(GetBotStatusTool(gateway))
    registry.register(SetExpressionTool(gateway))

    if spotify is not None:
        from ..spotify.tools import spotify_tools

        for tool in spotify_tools(spotify, gateway):
            registry.register(tool)

    return registry
