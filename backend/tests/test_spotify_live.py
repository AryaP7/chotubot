"""Opt-in live test against the real Spotify Web API.

Skipped unless credentials exist AND the marker is asked
for, so the normal suite stays offline and free:

    pytest -m spotify_live

WHAT THIS NEEDS
    A Spotify app (client id + secret), a refresh token
    from the Authorization Code flow with the scopes
    user-read-playback-state and user-modify-playback-state,
    and -- for anything that changes playback -- a Premium
    account with Spotify already open somewhere.

The playback tests are deliberately gentle: they read
state, and pause only if something was already playing,
then put it back. Running the suite should not hijack your
music.
"""

from __future__ import annotations

import os

import pytest

from chotubot.spotify import SpotifyStatus, SpotifyWebApi

HAS_CREDENTIALS = all(
    os.environ.get(var)
    for var in ("SPOTIFY_CLIENT_ID", "SPOTIFY_CLIENT_SECRET", "SPOTIFY_REFRESH_TOKEN")
)

pytestmark = [
    pytest.mark.spotify_live,
    pytest.mark.skipif(
        not HAS_CREDENTIALS,
        reason="needs SPOTIFY_CLIENT_ID / _SECRET / _REFRESH_TOKEN",
    ),
]


@pytest.fixture
def service():
    return SpotifyWebApi()


class TestLiveSpotify:
    async def test_token_exchange_works(self, service):
        token = await service._token()

        assert token
        # Never print it -- only its shape.
        assert len(token) > 20

    async def test_now_playing_returns_a_state(self, service):
        state = await service.now_playing()

        assert state.status in set(SpotifyStatus)
        print(f"\n  now playing: {state.describe()}")

    async def test_search_finds_a_well_known_track(self, service):
        hits = await service.search("Get Lucky Daft Punk", "track", limit=3)

        assert hits
        assert any("lucky" in hit.name.lower() for hit in hits)
        print(f"\n  found: {[h.describe() for h in hits]}")

    async def test_search_finds_a_playlist(self, service):
        hits = await service.search("Today's Top Hits", "playlist", limit=3)
        assert hits

    async def test_pause_and_restore_if_something_is_playing(self, service):
        """Only touches playback if it was already running."""
        before = await service.now_playing()

        if before.status is not SpotifyStatus.PLAYING:
            pytest.skip("nothing is playing; skipping the playback test")

        await service.pause()
        after = await service.now_playing()
        assert after.status is SpotifyStatus.PAUSED

        # Put it back the way it was found.
        await service.resume()

    async def test_protocol_payload_is_clean_on_real_data(self, service):
        state = await service.now_playing()
        payload = state.to_protocol_kwargs()

        blob = str(payload).lower()
        for forbidden in ("token", "secret", "bearer", "spotify:", "refresh"):
            assert forbidden not in blob
