"""SpotifyService: validation, the fake, and the real client.

The real client is exercised against a stub HTTP layer, so
these run with no Spotify account, no credentials and no
network.
"""

from __future__ import annotations

import pytest

from chotubot.spotify import (
    FakeSpotifyService,
    PlaybackState,
    SpotifyApiError,
    SpotifyAuthError,
    SpotifyError,
    SpotifyNoActiveDevice,
    SpotifyNotConfigured,
    SpotifyNotFound,
    SpotifyPremiumRequired,
    SpotifyRateLimited,
    SpotifyStatus,
    SpotifyWebApi,
    Track,
    validate_id,
    validate_query,
    validate_search_type,
)
from chotubot.spotify.web_api import make_spotify_service

GET_LUCKY = "2Foc5Q5nqNiosCNqttzHof"
BLINDING = "0VjIjW4GlUZAMYd2vXMi3b"


# --- validation ------------------------------------------


class TestValidation:
    def test_accepts_a_real_id(self):
        assert validate_id(GET_LUCKY) == GET_LUCKY

    def test_accepts_and_unwraps_a_uri(self):
        assert validate_id(f"spotify:track:{GET_LUCKY}") == GET_LUCKY

    @pytest.mark.parametrize(
        "bad",
        [
            "short",
            "way-too-long-to-be-a-spotify-id-at-all",
            "../../etc/passwd",
            "2Foc5Q5nqNiosCNqttzHo/",     # slash could alter a path
            "2Foc5Q5nqNiosCNqttzHo?",     # query separator
            "spotify:track:../../admin",
            "https://evil.example.com",
            "",
        ],
    )
    def test_rejects_anything_that_is_not_an_id(self, bad):
        with pytest.raises(SpotifyError):
            validate_id(bad)

    def test_rejects_non_string_id(self):
        with pytest.raises(SpotifyError):
            validate_id(12345)

    def test_query_must_be_non_empty_and_bounded(self):
        assert validate_query("  daft punk  ") == "daft punk"

        with pytest.raises(SpotifyError):
            validate_query("   ")
        with pytest.raises(SpotifyError):
            validate_query("x" * 500)

    def test_search_type_allowlist(self):
        assert validate_search_type("TRACK") == "track"

        with pytest.raises(SpotifyError, match="unknown search type"):
            validate_search_type("podcast")


# --- playback state --------------------------------------


class TestPlaybackState:
    def test_idle_describes_itself(self):
        assert "Nothing is playing" in PlaybackState().describe()

    def test_playing_reads_naturally(self):
        state = PlaybackState(
            status=SpotifyStatus.PLAYING,
            track=Track(GET_LUCKY, "Get Lucky", "Daft Punk", "RAM", 248000),
        )
        described = state.describe()

        assert "Playing" in described
        assert "Get Lucky" in described
        assert "Daft Punk" in described

    def test_protocol_payload_carries_no_identifiers(self):
        state = PlaybackState(
            status=SpotifyStatus.PLAYING,
            track=Track(GET_LUCKY, "Get Lucky", "Daft Punk", "RAM", 248000),
            progress_ms=1000,
            device="Living Room Speaker",
        )
        payload = state.to_protocol_kwargs()

        assert payload["playing"] is True
        assert payload["track"] == "Get Lucky"

        # No ids, no URIs, no device names, no tokens.
        blob = str(payload)
        assert GET_LUCKY not in blob
        assert "spotify:" not in blob
        assert "Living Room" not in blob

    def test_negative_progress_is_clamped(self):
        state = PlaybackState(status=SpotifyStatus.PLAYING, progress_ms=-50)
        assert state.to_protocol_kwargs()["progress_ms"] == 0


# --- the fake --------------------------------------------


class TestFakeService:
    async def test_starts_idle(self):
        service = FakeSpotifyService()
        state = await service.now_playing()

        assert state.status is SpotifyStatus.IDLE
        assert state.track is None

    async def test_resume_starts_something(self):
        service = FakeSpotifyService()
        state = await service.resume()

        assert state.status is SpotifyStatus.PLAYING
        assert state.track is not None

    async def test_pause_then_resume(self):
        service = FakeSpotifyService()
        await service.resume()

        assert (await service.pause()).status is SpotifyStatus.PAUSED
        assert (await service.resume()).status is SpotifyStatus.PLAYING

    async def test_next_and_previous_move_the_queue(self):
        service = FakeSpotifyService()
        first = (await service.resume()).track

        second = (await service.next_track()).track
        assert second != first

        back = (await service.previous_track()).track
        assert back == first

    async def test_search_finds_by_name_and_by_artist(self):
        service = FakeSpotifyService()

        by_name = await service.search("get lucky", "track")
        assert any(hit.name == "Get Lucky" for hit in by_name)

        by_artist = await service.search("daft punk", "track")
        assert len(by_artist) == 2

    async def test_search_respects_the_limit(self):
        service = FakeSpotifyService()
        assert len(await service.search("daft punk", "track", limit=1)) == 1

    async def test_search_for_nothing_returns_empty(self):
        service = FakeSpotifyService()
        assert await service.search("zzzzz", "track") == []

    async def test_play_track_by_id(self):
        service = FakeSpotifyService()
        state = await service.play_track(BLINDING)

        assert state.track.name == "Blinding Lights"
        assert state.status is SpotifyStatus.PLAYING

    async def test_play_unknown_id_is_not_found(self):
        service = FakeSpotifyService()

        with pytest.raises(SpotifyNotFound):
            await service.play_track("aaaaaaaaaaaaaaaaaaaaaa")

    async def test_play_malformed_id_is_rejected_before_anything_happens(self):
        service = FakeSpotifyService()

        with pytest.raises(SpotifyError):
            await service.play_track("not-an-id")

        assert "play_track" in service.calls
        assert service.state.track is None

    async def test_find_and_play_resolves_a_phrase(self):
        service = FakeSpotifyService()
        state = await service.find_and_play("coding", "playlist")

        assert state.status is SpotifyStatus.PLAYING
        assert "search:playlist" in service.calls
        assert "play_playlist" in service.calls

    async def test_find_and_play_artist_falls_back_to_a_track(self):
        service = FakeSpotifyService()
        state = await service.find_and_play("daft punk", "artist")

        # No "play an artist" endpoint exists.
        assert "search:track" in service.calls
        assert state.track.artist == "Daft Punk"

    async def test_find_and_play_nothing_found(self):
        service = FakeSpotifyService()

        with pytest.raises(SpotifyNotFound):
            await service.find_and_play("nonexistent band", "track")

    async def test_no_device_blocks_playback_but_not_reads(self):
        service = FakeSpotifyService(has_device=False)

        # Reading is fine.
        assert (await service.now_playing()).status is SpotifyStatus.IDLE

        for action in (service.resume, service.pause,
                       service.next_track, service.previous_track):
            with pytest.raises(SpotifyNoActiveDevice):
                await action()


# --- the real client, stubbed ----------------------------


class StubResponse:
    def __init__(self, status_code=200, payload=None, headers=None, text=""):
        self.status_code = status_code
        self._payload = payload if payload is not None else {}
        self.headers = headers or {}
        self.text = text

    def json(self):
        return self._payload


class StubHttp:
    """Replays queued responses and records every request."""

    def __init__(self, responses=None):
        self.responses = list(responses or [])
        self.requests: list[tuple[str, str, dict]] = []

    def _next(self):
        if not self.responses:
            return StubResponse(200, {})
        item = self.responses.pop(0)
        if isinstance(item, Exception):
            raise item
        return item

    async def post(self, url, **kwargs):
        self.requests.append(("POST", url, kwargs))
        return self._next()

    async def request(self, method, url, **kwargs):
        self.requests.append((method, url, kwargs))
        return self._next()


def token_response():
    return StubResponse(200, {"access_token": "fake-token", "expires_in": 3600})


def api(responses, **kwargs):
    http = StubHttp([token_response(), *responses])
    return (
        SpotifyWebApi(
            client_id="cid",
            client_secret="secret",
            refresh_token="refresh",
            http=http,
            **kwargs,
        ),
        http,
    )


class TestWebApiConfiguration:
    def test_missing_credentials_is_a_clean_error(self, monkeypatch):
        for var in ("SPOTIFY_CLIENT_ID", "SPOTIFY_CLIENT_SECRET",
                    "SPOTIFY_REFRESH_TOKEN"):
            monkeypatch.delenv(var, raising=False)

        with pytest.raises(SpotifyNotConfigured, match="SPOTIFY_CLIENT_ID"):
            SpotifyWebApi()

    def test_factory_defaults_to_the_fake(self, monkeypatch):
        monkeypatch.delenv("SPOTIFY_SERVICE", raising=False)
        assert make_spotify_service().name == "fake"

    def test_factory_rejects_an_unknown_name(self):
        with pytest.raises(SpotifyNotConfigured, match="unknown SPOTIFY_SERVICE"):
            make_spotify_service("napster")


class TestWebApiAuth:
    async def test_token_is_fetched_once_and_reused(self):
        service, http = api([
            StubResponse(200, {"is_playing": False, "item": None}),
            StubResponse(200, {"is_playing": False, "item": None}),
        ])

        await service.now_playing()
        await service.now_playing()

        token_calls = [r for r in http.requests if "accounts.spotify.com" in r[1]]
        assert len(token_calls) == 1

    async def test_credentials_are_sent_as_basic_auth_not_in_a_url(self):
        service, http = api([StubResponse(200, {})])
        await service.now_playing()

        method, url, kwargs = http.requests[0]
        assert url == "https://accounts.spotify.com/api/token"
        assert "Basic " in kwargs["headers"]["Authorization"]
        assert "secret" not in url

    async def test_rejected_refresh_token_is_an_auth_error(self):
        http = StubHttp([StubResponse(400, {"error": "invalid_grant"})])
        service = SpotifyWebApi(
            client_id="c", client_secret="s", refresh_token="bad", http=http
        )

        with pytest.raises(SpotifyAuthError, match="rejected the credentials"):
            await service.now_playing()


class TestWebApiErrorMapping:
    async def test_403_means_premium(self):
        service, _ = api([StubResponse(403, text="Player command failed")])

        with pytest.raises(SpotifyPremiumRequired):
            await service.resume()

    async def test_429_carries_the_retry_delay(self):
        service, _ = api([StubResponse(429, headers={"Retry-After": "7"})])

        with pytest.raises(SpotifyRateLimited) as caught:
            await service.resume()
        assert caught.value.retry_after_s == 7

    async def test_404_on_a_command_means_no_device(self):
        service, _ = api([StubResponse(404)])

        with pytest.raises(SpotifyNoActiveDevice, match="Open Spotify"):
            await service.resume()

    async def test_404_on_a_read_is_just_idle(self):
        service, _ = api([StubResponse(404)])

        state = await service.now_playing()
        assert state.status is SpotifyStatus.IDLE

    async def test_500_is_an_api_error(self):
        service, _ = api([StubResponse(500, text="upstream boom")])

        with pytest.raises(SpotifyApiError) as caught:
            await service.resume()
        assert caught.value.status == 500

    async def test_401_clears_the_cached_token(self):
        service, _ = api([StubResponse(401)])

        with pytest.raises(SpotifyAuthError):
            await service.now_playing()
        assert service._access_token == ""


class TestWebApiRequests:
    async def test_now_playing_parses_a_real_shape(self):
        service, _ = api([
            StubResponse(200, {
                "is_playing": True,
                "progress_ms": 42000,
                "device": {"name": "Kitchen"},
                "item": {
                    "id": GET_LUCKY,
                    "name": "Get Lucky",
                    "duration_ms": 248000,
                    "artists": [{"name": "Daft Punk"}],
                    "album": {"name": "Random Access Memories"},
                },
            })
        ])

        state = await service.now_playing()

        assert state.status is SpotifyStatus.PLAYING
        assert state.track.name == "Get Lucky"
        assert state.track.artist == "Daft Punk"
        assert state.progress_ms == 42000

    async def test_empty_player_response_is_idle(self):
        service, _ = api([StubResponse(204)])
        assert (await service.now_playing()).status is SpotifyStatus.IDLE

    async def test_play_track_sends_a_uri_not_a_url(self):
        service, http = api([StubResponse(204), StubResponse(200, {})])
        await service.play_track(GET_LUCKY)

        play = [r for r in http.requests if r[1].endswith("/me/player/play")][0]
        assert play[2]["json"] == {"uris": [f"spotify:track:{GET_LUCKY}"]}

    async def test_play_playlist_uses_a_context_uri(self):
        service, http = api([StubResponse(204), StubResponse(200, {})])
        await service.play_playlist("37i9dQZF1DX5trt9i14X7j")

        play = [r for r in http.requests if r[1].endswith("/me/player/play")][0]
        assert play[2]["json"]["context_uri"].startswith("spotify:playlist:")

    async def test_bad_id_never_reaches_the_network(self):
        service, http = api([])
        before = len(http.requests)

        with pytest.raises(SpotifyError):
            await service.play_track("https://evil.example.com/x")

        assert len(http.requests) == before

    async def test_search_passes_query_as_a_parameter(self):
        service, http = api([
            StubResponse(200, {
                "tracks": {"items": [{
                    "id": GET_LUCKY,
                    "name": "Get Lucky",
                    "artists": [{"name": "Daft Punk"}],
                }]}
            })
        ])

        hits = await service.search("daft punk", "track")

        search = [r for r in http.requests if "/search" in r[1]][0]
        assert search[2]["params"]["q"] == "daft punk"
        assert search[2]["params"]["type"] == "track"

        assert hits[0].name == "Get Lucky"
        assert hits[0].owner == "Daft Punk"

    async def test_search_skips_malformed_items(self):
        service, _ = api([
            StubResponse(200, {"tracks": {"items": [None, {}, {"id": GET_LUCKY,
                                                              "name": "ok"}]}})
        ])

        hits = await service.search("x", "track")
        assert len(hits) == 1

    async def test_endpoints_are_constants_not_model_input(self):
        service, http = api([StubResponse(204), StubResponse(200, {})])
        await service.pause()

        for _, url, _ in http.requests:
            assert url.startswith("https://api.spotify.com/v1") or (
                url.startswith("https://accounts.spotify.com")
            )
