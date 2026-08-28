"""Backend AI agent.

Provider-agnostic by construction:

    LLMProvider  ->  Agent  ->  ToolRegistry  ->  Tool

The Agent never imports a vendor SDK. Tools reach the ESP32
through BotGateway, which uses Protocol v1's own message
builders -- there is no second protocol.
"""

from .agent import Agent, AgentResponse, ExecutedTool, build_agent
from .builtin_tools import (
    GetBotStatusTool,
    GetTimeTool,
    SetExpressionTool,
    default_registry,
)
from .factory import available_providers, make_provider
from .gateway import BotGateway, BotStatus, HubGateway, NullGateway
from .messages import (
    Conversation,
    ConversationStore,
    InMemoryConversationStore,
    Message,
    Role,
)
from .providers import (
    FailingProvider,
    FakeLLMProvider,
    LLMProvider,
    LLMResponse,
    ProviderError,
    ScriptedProvider,
    ToolCall,
)
from .tools import Tool, ToolError, ToolParameter, ToolRegistry, ToolResult

__all__ = [
    "Agent",
    "AgentResponse",
    "ExecutedTool",
    "build_agent",
    "BotGateway",
    "BotStatus",
    "HubGateway",
    "NullGateway",
    "make_provider",
    "available_providers",
    "Conversation",
    "ConversationStore",
    "InMemoryConversationStore",
    "Message",
    "Role",
    "LLMProvider",
    "LLMResponse",
    "ProviderError",
    "FakeLLMProvider",
    "ScriptedProvider",
    "FailingProvider",
    "ToolCall",
    "Tool",
    "ToolError",
    "ToolParameter",
    "ToolRegistry",
    "ToolResult",
    "GetTimeTool",
    "GetBotStatusTool",
    "SetExpressionTool",
    "default_registry",
]
