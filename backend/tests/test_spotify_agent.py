"""Spotify through the Agent, and out to the bot.

Covers the full path:

    user text -> Agent -> ToolRegistry -> Spotify tool
              -> SpotifyService -> BotGateway -> Protocol v1
"""

from __future__ import annotations

import pytest

from chotubot import protocol, server
from chotubot.agent import Agent, NullGateway, build_agent, default_registry
from chotubot.spotify import FakeSpotifyService, SpotifyStatus
from conftest import json_dumps, recv_json

GET_LUCKY = "2Foc5Q5nqNiosCNqttzHof"


@pytest.fixture
def spotify():
    return FakeSpotifyService()


@pytest.fixture
def gateway():
    return NullGateway(connected=1)


@pytest.fixture
def agent(gateway, spotify):
    return build_agent(gateway, spotify=spotify)


class TestRegistration:
    def test_all_ten_tools_are_registered(self, gateway, spotify):
        names = set(default_registry(gateway, spotify).names())

        for expected in [
            "spotify_play",
            "spotify_pause",
            "spotify_resume",
            "spotify_next",
            "spotify_previous",
            "spotify_now_playing",
            "spotify_search",
            "spotify_play_track",
            "spotify_play_playlist",
            "spotify_play_album",
        ]:
            assert expected in names

    def test_without_a_service_there_are_no_spotify_tools(self, gateway):
        names = default_registry(gateway).names()
        assert not any(n.startswith("spotify_") for n in names)

    def test_search_tool_declares_its_allowlist(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        spec = next(s for s in registry.specs() if s["name"] == "spotify_search")

        kind = next(p for p in spec["parameters"] if p["name"] == "kind")
        assert set(kind["enum"]) == {"track", "album", "playlist", "artist"}


class TestToolExecution:
    async def test_now_playing_when_idle(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute("spotify_now_playing", {})

        assert result.ok
        assert "Nothing is playing" in result.content

    async def test_play_track_by_name(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute("spotify_play_track", {"query": "get lucky"})

        assert result.ok
        assert "Get Lucky" in result.content
        assert spotify.state.status is SpotifyStatus.PLAYING

    async def test_play_playlist_by_name(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute(
            "spotify_play_playlist", {"query": "coding"}
        )

        assert result.ok
        assert "play_playlist" in spotify.calls

    async def test_transport_controls(self, gateway, spotify):
        registry = default_registry(gateway, spotify)

        await registry.execute("spotify_play", {})
        assert spotify.state.status is SpotifyStatus.PLAYING

        await registry.execute("spotify_pause", {})
        assert spotify.state.status is SpotifyStatus.PAUSED

        await registry.execute("spotify_resume", {})
        assert spotify.state.status is SpotifyStatus.PLAYING

        first = spotify.state.track
        await registry.execute("spotify_next", {})
        assert spotify.state.track != first

        await registry.execute("spotify_previous", {})
        assert spotify.state.track == first

    async def test_search_lists_without_playing(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute(
            "spotify_search", {"query": "daft punk", "kind": "track"}
        )

        assert result.ok
        assert "Get Lucky" in result.content
        assert spotify.state.track is None   # nothing started

    async def test_search_with_no_results_is_not_a_failure(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute(
            "spotify_search", {"query": "zzzzz", "kind": "track"}
        )

        assert result.ok
        assert "No track found" in result.content


class TestToolValidation:
    async def test_bad_search_kind_rejected_by_the_registry(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute(
            "spotify_search", {"query": "x", "kind": "podcast"}
        )

        assert not result.ok
        assert "must be one of" in result.error

    async def test_missing_argument_rejected(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute("spotify_play_track", {})

        assert not result.ok
        assert "missing required argument" in result.error

    async def test_extra_argument_rejected(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute(
            "spotify_play_track", {"query": "x", "device_id": "sneaky"}
        )

        assert not result.ok
        assert "unexpected argument" in result.error

    async def test_empty_query_is_a_clean_failure(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute("spotify_play_track", {"query": "   "})

        assert not result.ok
        assert "empty" in result.error


class TestErrorHandling:
    async def test_no_device_is_reported_with_advice(self, gateway):
        offline = FakeSpotifyService(has_device=False)
        registry = default_registry(gateway, offline)

        result = await registry.execute("spotify_play", {})

        assert not result.ok
        assert "Open Spotify" in result.error

    async def test_unknown_item_is_reported(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        result = await registry.execute(
            "spotify_play_track", {"query": "a song that does not exist"}
        )

        assert not result.ok
        assert "no track found" in result.error.lower()

    async def test_service_crash_becomes_a_result(self, gateway, spotify, monkeypatch):
        async def boom():
            raise RuntimeError("spotify exploded")

        monkeypatch.setattr(spotify, "now_playing", boom)
        registry = default_registry(gateway, spotify)

        result = await registry.execute("spotify_now_playing", {})

        assert not result.ok
        assert "RuntimeError" in result.error

    async def test_unreachable_bot_does_not_fail_playback(self, spotify, monkeypatch):
        gateway = NullGateway(connected=1)

        async def boom(**kwargs):
            raise ConnectionError("bot is gone")

        monkeypatch.setattr(gateway, "send_spotify_state", boom)
        registry = default_registry(gateway, spotify)

        result = await registry.execute("spotify_play_track", {"query": "get lucky"})

        # The music started; only the display update failed.
        assert result.ok
        assert spotify.state.status is SpotifyStatus.PLAYING


class TestBotGateway:
    async def test_playing_pushes_protocol_v1_state(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        await registry.execute("spotify_play_track", {"query": "get lucky"})

        sent = [m for m in gateway.sent if m["type"] == "spotify_state"]
        assert len(sent) == 1

        message = sent[0]
        assert message["playing"] is True
        assert message["track"] == "Get Lucky"
        assert message["artist"] == "Daft Punk"
        assert message["duration_ms"] == 248000

    async def test_pause_pushes_playing_false(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        await registry.execute("spotify_play", {})
        await registry.execute("spotify_pause", {})

        last = [m for m in gateway.sent if m["type"] == "spotify_state"][-1]
        assert last["playing"] is False
        assert last["status"] == "paused"

    async def test_search_does_not_push_state(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        await registry.execute(
            "spotify_search", {"query": "daft punk", "kind": "track"}
        )

        assert not [m for m in gateway.sent if m["type"] == "spotify_state"]

    async def test_no_credentials_ever_reach_the_bot(self, gateway, spotify):
        registry = default_registry(gateway, spotify)
        await registry.execute("spotify_play_track", {"query": "get lucky"})

        blob = str(gateway.sent).lower()
        for forbidden in ("token", "secret", "client_id", "refresh",
                          "authorization", "bearer", "spotify:"):
            assert forbidden not in blob


class TestAgentSelectsSpotifyTools:
    @pytest.mark.parametrize(
        "utterance,expected",
        [
            ("Pause Spotify.", "spotify_pause"),
            ("Skip this song.", "spotify_next"),
            ("Go back to the previous track.", "spotify_previous"),
            ("Resume.", "spotify_resume"),
            ("What song is playing?", "spotify_now_playing"),
            ("What's playing?", "spotify_now_playing"),
            ("Play my coding playlist.", "spotify_play_playlist"),
            ("Play Get Lucky.", "spotify_play_track"),
            ("Play Spotify.", "spotify_play"),
        ],
    )
    async def test_offline_provider_routes_the_examples(
        self, agent, utterance, expected
    ):
        response = await agent.send(utterance, conversation_id="c1")
        assert expected in response.tool_names

    async def test_playlist_request_reaches_the_bot(self, agent, gateway):
        response = await agent.send(
            "Play my coding playlist.", conversation_id="c1"
        )

        assert response.ok
        sent = [m for m in gateway.sent if m["type"] == "spotify_state"]
        assert sent and sent[0]["playing"] is True

    async def test_non_spotify_requests_are_unaffected(self, agent):
        response = await agent.send("What time is it?", conversation_id="c1")
        assert response.tool_names == ["get_time"]

        response = await agent.send("Look happy.", conversation_id="c2")
        assert response.tool_names == ["set_expression"]


class TestSimulatedBotReceivesState:
    """Over a real socket, to a client speaking the firmware's protocol."""

    async def test_spotify_state_arrives_at_a_connected_bot(self, bot, control):
        service = FakeSpotifyService()
        server.agent.registry = default_registry(
            server.agent.registry.get("get_bot_status")._gateway, service
        )

        await control.send(
            json_dumps(
                {
                    "action": "chat",
                    "text": "Play my coding playlist.",
                    "conversation_id": "spotify-1",
                }
            )
        )

        delivered = await recv_json(bot)

        assert delivered["type"] == "spotify_state"
        assert delivered["playing"] is True
        assert delivered["track"]

        reply = await recv_json(control)
        assert reply["ok"] is True

    async def test_direct_control_spotify_message_still_works(self, bot, control):
        # The pre-existing control action must not regress.
        await control.send(
            json_dumps(
                {
                    "action": "spotify",
                    "playing": True,
                    "track": "Manual Track",
                    "artist": "Manual Artist",
                }
            )
        )

        delivered = await recv_json(bot)
        assert delivered["track"] == "Manual Track"
