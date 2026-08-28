"""End-to-end agent demo.

Starts a server, attaches a simulated bot, and walks the
agent through the full loop:

    user text -> agent -> LLM provider -> tool call
              -> registry -> tool result -> provider
              -> final response, and an expression on the bot

Offline, no key needed:

    python tools/demo_agent.py

Against the real model (costs money, needs network):

    export ANTHROPIC_API_KEY=sk-ant-...
    python tools/demo_agent.py --real

The BOT is simulated in both cases -- no ESP32 has ever run
this firmware. Only the LLM changes.
"""

from __future__ import annotations

import asyncio
import json
import sys
from pathlib import Path

# Python puts this script's directory on sys.path, not the
# working directory, so the package needs adding explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import websockets  # noqa: E402
from websockets.asyncio.server import serve  # noqa: E402

from chotubot import protocol, server  # noqa: E402

TOKEN = "demo-token"


def rule(title: str) -> None:
    print(f"\n{'=' * 62}\n  {title}\n{'=' * 62}")


async def fake_bot(uri: str, seen: list[dict]) -> None:
    """A socket speaking the firmware's protocol."""
    async with websockets.connect(f"{uri}/bot") as ws:
        await ws.send(
            json.dumps(
                {
                    "type": "hello",
                    "token": TOKEN,
                    "fw": "demo-bot",
                    "v": protocol.PROTOCOL_VERSION,
                }
            )
        )

        try:
            async for raw in ws:
                if isinstance(raw, bytes):
                    continue

                message = json.loads(raw)
                seen.append(message)

                if message.get("type") == "expression":
                    print(f"      [BOT] display now shows: {message['value']}")
        except websockets.ConnectionClosed:
            pass


async def main() -> int:
    use_real = "--real" in sys.argv

    server.TOKEN = TOKEN
    server.hub.sessions.clear()

    if use_real:
        from chotubot.agent import ProviderError, make_provider

        try:
            server.agent.provider = make_provider("anthropic")
        except ProviderError as exc:
            print(f"\ncannot use the real provider: {exc}\n")
            return 1

    provider = server.agent.provider
    label = "REAL LLM" if use_real else "SIMULATED LLM (offline rules)"
    detail = getattr(provider, "model", "keyword matcher")

    print(f"\n  LLM      : {label}  [{provider.name}: {detail}]")
    print("  HARDWARE : SIMULATED (no ESP32 has ever run this firmware)")

    async with serve(server.handle_connection, "127.0.0.1", 0) as ws_server:
        port = ws_server.sockets[0].getsockname()[1]
        uri = f"ws://127.0.0.1:{port}"

        seen: list[dict] = []
        bot_task = asyncio.create_task(fake_bot(uri, seen))
        await asyncio.sleep(0.4)

        rule("1. bot connected over Protocol v1")
        status = server.agent.registry.get("get_bot_status")
        print(f"   {(await status.run()).content}")
        print(f"   tools available: {', '.join(server.agent.registry.names())}")

        conversations = [
            ("What time is it?", "expects the get_time tool"),
            ("Look happy!", "expects set_expression -> reaches the bot"),
            ("What is your status?", "expects get_bot_status"),
            (
                "Tell me something interesting in one sentence."
                if use_real
                else "Tell me something",
                "conversational: expects no tool at all",
            ),
        ]

        for text, expectation in conversations:
            rule(f"user: {text}")
            print(f"   ({expectation})")

            reply = await server.agent.send(text, conversation_id="demo")
            await asyncio.sleep(0.2)

            if not reply.ok:
                print(f"   ERROR: {reply.error}")

            for used in reply.tools_used:
                mark = "ok" if used.result.ok else "FAILED"
                print(f"   tool: {used.name}{used.arguments} -> {mark}")
                print(f"         {used.result.content}")

            print(f"   agent: {reply.text}")

        rule("2. error handling: an invented tool never runs")
        bad = await server.agent.registry.execute("delete_everything", {})
        print(f"   {bad.error}")

        bad = await server.agent.registry.execute(
            "set_expression", {"expression": "wizard"}
        )
        print(f"   {bad.error}")

        rule("3. conversation memory")
        conversation = server.agent.store.get("demo")
        print(f"   {len(conversation.messages)} messages retained")
        print(f"   roles: {[m.role.value for m in conversation.messages][:8]} ...")

        server.agent.reset("demo")
        print(f"   after reset: {len(conversation.messages)} message (system prompt)")

        rule("4. what actually reached the bot")
        for message in seen:
            print(f"   {json.dumps(message)}")

        bot_task.cancel()

    if use_real:
        print("\nLLM: real API.  HARDWARE: simulated throughout.\n")
    else:
        print("\nno hardware, no API key, no network was used.\n")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
