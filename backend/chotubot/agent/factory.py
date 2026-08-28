"""Provider selection, driven entirely by the environment.

    LLM_PROVIDER=fake        offline rules (default)
    LLM_PROVIDER=anthropic   the real Messages API

Kept apart from providers.py so importing the fake ones
never pulls in a vendor SDK.
"""

from __future__ import annotations

import logging
import os

from .providers import FakeLLMProvider, LLMProvider, ProviderError

log = logging.getLogger("chotubot.agent")

# Fake by default. A missing LLM_PROVIDER must never
# silently start spending money, and the offline path is
# the one the test suite depends on.
DEFAULT_PROVIDER = "fake"


def available_providers() -> list[str]:
    return ["fake", "anthropic"]


def make_provider(name: str | None = None) -> LLMProvider:
    """Build the configured provider.

    Raises ProviderError with an actionable message rather
    than falling back silently -- asking for the real
    provider and quietly getting the fake one would be a
    very confusing bug.
    """
    choice = (name or os.environ.get("LLM_PROVIDER", DEFAULT_PROVIDER)).lower().strip()

    if choice == "fake":
        return FakeLLMProvider()

    if choice == "anthropic":
        # Imported here so the SDK is only required when it
        # is actually the selected provider.
        from .anthropic_provider import AnthropicProvider

        provider = AnthropicProvider()
        log.info(
            "using the Anthropic provider (model=%s, effort=%s)",
            provider.model,
            provider.effort,
        )
        return provider

    raise ProviderError(
        f"unknown LLM_PROVIDER {choice!r}; valid: {', '.join(available_providers())}"
    )
