"""Tool registry.

The security boundary of the whole agent. Two rules:

1. A tool exists only if it was registered. The model
   cannot name one into existence.
2. Arguments are checked against a declared schema before
   the tool body runs.

There is deliberately no shell tool, no HTTP tool, no
filesystem tool, and no eval. PC control arrives later as
individually declared, individually validated tools.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any


@dataclass
class ToolResult:
    """What a tool hands back to the agent.

    A failure is a value, not an exception: the model needs
    to see what went wrong so it can try something else.
    """

    ok: bool
    content: str
    error: str | None = None

    @classmethod
    def success(cls, content: str) -> ToolResult:
        return cls(ok=True, content=content)

    @classmethod
    def failure(cls, error: str) -> ToolResult:
        return cls(ok=False, content=f"Tool failed: {error}", error=error)


@dataclass
class ToolParameter:
    name: str
    description: str
    required: bool = True
    # Only the handful we actually need. Deliberately not a
    # full JSON Schema implementation.
    type: str = "string"
    enum: list[str] | None = None


class Tool(ABC):
    """One capability the agent may invoke."""

    name: str = ""
    description: str = ""
    parameters: list[ToolParameter] = []

    @abstractmethod
    async def run(self, **kwargs: Any) -> ToolResult: ...

    def spec(self) -> dict:
        """Description handed to the LLM."""
        return {
            "name": self.name,
            "description": self.description,
            "parameters": [
                {
                    "name": p.name,
                    "type": p.type,
                    "description": p.description,
                    "required": p.required,
                    **({"enum": p.enum} if p.enum else {}),
                }
                for p in self.parameters
            ],
        }


class ToolError(Exception):
    """Raised for a call that must not reach a tool body."""


@dataclass
class ToolRegistry:
    _tools: dict[str, Tool] = field(default_factory=dict)

    def register(self, tool: Tool) -> None:
        if not tool.name:
            raise ValueError("tool must have a name")
        if tool.name in self._tools:
            raise ValueError(f"tool {tool.name!r} already registered")
        self._tools[tool.name] = tool

    def get(self, name: str) -> Tool | None:
        return self._tools.get(name)

    def names(self) -> list[str]:
        return sorted(self._tools)

    def specs(self) -> list[dict]:
        return [t.spec() for t in self._tools.values()]

    def validate(self, name: str, arguments: dict[str, Any]) -> None:
        """Check a call without running it.

        Raises ToolError describing the first problem found.
        """
        tool = self._tools.get(name)
        if tool is None:
            raise ToolError(
                f"unknown tool {name!r}; available: {', '.join(self.names())}"
            )

        if not isinstance(arguments, dict):
            raise ToolError("arguments must be an object")

        declared = {p.name: p for p in tool.parameters}

        for extra in set(arguments) - set(declared):
            raise ToolError(f"unexpected argument {extra!r} for {name!r}")

        for param in tool.parameters:
            if param.required and param.name not in arguments:
                raise ToolError(f"missing required argument {param.name!r}")

            if param.name not in arguments:
                continue

            value = arguments[param.name]

            if param.type == "string" and not isinstance(value, str):
                raise ToolError(f"{param.name!r} must be a string")
            if param.type == "integer" and (
                isinstance(value, bool) or not isinstance(value, int)
            ):
                raise ToolError(f"{param.name!r} must be an integer")

            if param.enum is not None and value not in param.enum:
                raise ToolError(
                    f"{param.name!r} must be one of: {', '.join(param.enum)}"
                )

    async def execute(self, name: str, arguments: dict[str, Any]) -> ToolResult:
        """Validate then run. Never raises for a bad call."""
        try:
            self.validate(name, arguments)
        except ToolError as exc:
            return ToolResult.failure(str(exc))

        tool = self._tools[name]

        try:
            return await tool.run(**arguments)
        except Exception as exc:  # noqa: BLE001
            # A tool crashing must not take the agent down.
            return ToolResult.failure(f"{type(exc).__name__}: {exc}")
