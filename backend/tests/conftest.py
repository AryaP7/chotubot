"""Shared fixtures.

Each test gets a real server on a real ephemeral port. No
mocking of the transport -- if the WebSocket layer breaks,
these tests break, which is the point.
"""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator

import pytest
import pytest_asyncio
import websockets
from websockets.asyncio.server import serve

from chotubot import protocol, server

TEST_TOKEN = "test-token-for-pytest"


@pytest_asyncio.fixture
async def backend(monkeypatch) -> AsyncIterator[str]:
    """Start a server on a free port; yield its base URI."""
    monkeypatch.setattr(server, "TOKEN", TEST_TOKEN)

    # The hub is module state; a leaked session from a
    # previous test would show up in broadcast counts.
    server.hub.sessions.clear()

    async with serve(server.handle_connection, "127.0.0.1", 0) as ws_server:
        port = ws_server.sockets[0].getsockname()[1]
        yield f"ws://127.0.0.1:{port}"

    server.hub.sessions.clear()


@pytest_asyncio.fixture
async def bot(backend: str) -> AsyncIterator[websockets.ClientConnection]:
    """An authenticated bot connection, welcome consumed."""
    async with websockets.connect(f"{backend}/bot") as ws:
        await ws.send(
            json_dumps(
                {
                    "type": "hello",
                    "token": TEST_TOKEN,
                    "fw": "pytest",
                    "v": protocol.PROTOCOL_VERSION,
                }
            )
        )
        # welcome, then the greeting expression
        await recv_json(ws)
        await recv_json(ws)
        yield ws


@pytest_asyncio.fixture
async def control(backend: str) -> AsyncIterator[websockets.ClientConnection]:
    """An authenticated control client."""
    async with websockets.connect(f"{backend}/control") as ws:
        await ws.send(json_dumps({"token": TEST_TOKEN}))
        await recv_json(ws)
        yield ws


# --- helpers, imported by the test modules ---------------

import json  # noqa: E402


def json_dumps(payload: dict) -> str:
    return json.dumps(payload)


async def recv_json(ws, timeout: float = 2.0) -> dict:
    raw = await asyncio.wait_for(ws.recv(), timeout)
    assert isinstance(raw, str), f"expected text frame, got {len(raw)} bytes"
    return json.loads(raw)


async def expect_closed(ws, code: int | None = None, timeout: float = 2.0) -> int:
    """Assert the server closed the connection, return the code."""
    with pytest.raises(websockets.ConnectionClosed) as caught:
        await asyncio.wait_for(ws.recv(), timeout)
        await asyncio.wait_for(ws.recv(), timeout)

    actual = caught.value.rcvd.code if caught.value.rcvd else None
    if code is not None:
        assert actual == code, f"expected close {code}, got {actual}"
    return actual
