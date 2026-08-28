"""Tool registry: the agent's security boundary."""

from __future__ import annotations

import pytest

from chotubot import protocol
from chotubot.agent import (
    GetBotStatusTool,
    GetTimeTool,
    NullGateway,
    SetExpressionTool,
    Tool,
    ToolError,
    ToolParameter,
    ToolRegistry,
    ToolResult,
    default_registry,
)


class ExplodingTool(Tool):
    name = "explode"
    description = "Always raises."
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        raise RuntimeError("boom")


class EchoTool(Tool):
    name = "echo"
    description = "Echoes text back."
    parameters = [ToolParameter(name="text", description="What to echo.")]

    async def run(self, text: str) -> ToolResult:
        return ToolResult.success(text)


class TestRegistration:
    def test_registers_and_lists(self):
        registry = ToolRegistry()
        registry.register(EchoTool())
        assert registry.names() == ["echo"]

    def test_duplicate_name_rejected(self):
        registry = ToolRegistry()
        registry.register(EchoTool())
        with pytest.raises(ValueError, match="already registered"):
            registry.register(EchoTool())

    def test_default_registry_has_exactly_the_three_tools(self):
        registry = default_registry(NullGateway())
        assert registry.names() == ["get_bot_status", "get_time", "set_expression"]

    def test_no_dangerous_tools_registered(self):
        # Guards against a later phase quietly adding one.
        names = set(default_registry(NullGateway()).names())
        forbidden = {"shell", "exec", "eval", "http", "request", "read_file", "python"}
        assert not (names & forbidden)


class TestValidation:
    def test_unknown_tool_rejected(self):
        registry = default_registry(NullGateway())
        with pytest.raises(ToolError, match="unknown tool"):
            registry.validate("rm_rf", {})

    def test_missing_required_argument(self):
        registry = ToolRegistry()
        registry.register(EchoTool())
        with pytest.raises(ToolError, match="missing required argument"):
            registry.validate("echo", {})

    def test_unexpected_argument_rejected(self):
        registry = ToolRegistry()
        registry.register(EchoTool())
        with pytest.raises(ToolError, match="unexpected argument"):
            registry.validate("echo", {"text": "hi", "sudo": True})

    def test_wrong_type_rejected(self):
        registry = ToolRegistry()
        registry.register(EchoTool())
        with pytest.raises(ToolError, match="must be a string"):
            registry.validate("echo", {"text": 42})

    def test_arguments_must_be_object(self):
        registry = ToolRegistry()
        registry.register(EchoTool())
        with pytest.raises(ToolError, match="must be an object"):
            registry.validate("echo", ["hi"])

    def test_expression_enum_enforced(self):
        registry = default_registry(NullGateway())
        with pytest.raises(ToolError, match="must be one of"):
            registry.validate("set_expression", {"expression": "wizard"})

    def test_every_protocol_expression_passes_validation(self):
        # The tool's enum and the protocol allowlist must not
        # drift apart.
        registry = default_registry(NullGateway())
        for expression in protocol.EXPRESSIONS:
            registry.validate("set_expression", {"expression": expression})


class TestExecution:
    async def test_get_time_returns_something_timelike(self):
        result = await GetTimeTool().run()
        assert result.ok
        assert ":" in result.content

    async def test_bot_status_reports_offline(self):
        result = await GetBotStatusTool(NullGateway(connected=0)).run()
        assert result.ok
        assert "No bot is connected" in result.content

    async def test_bot_status_reports_online(self):
        result = await GetBotStatusTool(NullGateway(connected=2)).run()
        assert "2 bot(s) connected" in result.content

    async def test_set_expression_sends_protocol_message(self):
        gateway = NullGateway(connected=1)
        result = await SetExpressionTool(gateway).run(expression="happy")

        assert result.ok
        assert gateway.sent == [{"type": "expression", "value": "happy"}]

    async def test_set_expression_with_no_bot_is_honest(self):
        gateway = NullGateway(connected=0)
        result = await SetExpressionTool(gateway).run(expression="happy")

        # Succeeds, but says plainly that nothing displayed it.
        assert result.ok
        assert "No bot is connected" in result.content

    async def test_tool_exception_becomes_a_result(self):
        registry = ToolRegistry()
        registry.register(ExplodingTool())

        result = await registry.execute("explode", {})

        assert not result.ok
        assert "RuntimeError" in result.error

    async def test_execute_never_raises_for_bad_call(self):
        registry = default_registry(NullGateway())

        for name, args in [
            ("nonexistent", {}),
            ("set_expression", {}),
            ("set_expression", {"expression": "wizard"}),
            ("get_time", {"unexpected": 1}),
        ]:
            result = await registry.execute(name, args)
            assert not result.ok
            assert result.error


class TestSpecs:
    def test_spec_exposes_enum_for_the_model(self):
        registry = default_registry(NullGateway())
        spec = next(s for s in registry.specs() if s["name"] == "set_expression")

        enum = spec["parameters"][0]["enum"]
        assert set(enum) == set(protocol.EXPRESSIONS)
