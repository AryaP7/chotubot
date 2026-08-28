"""The real Spotify Web API client.

Isolated here so nothing else in the project knows what a
Spotify endpoint looks like. Every URL in this file is a
constant; the model contributes only a validated search
phrase or a validated 22-character id.

AUTHENTICATION
    Authorization Code flow with a long-lived refresh
    token. The token exchange happens here, backend-side.
    Credentials come from the environment and are never
    logged, returned, or sent to the ESP32.

WHAT NEEDS PREMIUM
    Playback control (play/pause/next/previous) is a
    Premium-only API. Search and now-playing work on a free
    account. A free account gets a clear error, not a
    silent no-op.
"""

from __future__ import annotations

import base64
import logging
import os
import time
from typing import Any

from .models import (
    PlaybackState,
    SearchHit,
    SpotifyApiError,
    SpotifyAuthError,
    SpotifyNoActiveDevice,
    SpotifyNotConfigured,
    SpotifyPremiumRequired,
    SpotifyRateLimited,
    SpotifyStatus,
    Track,
    validate_id,
    validate_query,
    validate_search_type,
)
from .service import SpotifyService

log = logging.getLogger("chotubot.spotify")

API = "https://api.spotify.com/v1"
TOKEN_URL = "https://accounts.spotify.com/api/token"

# Scopes the refresh token must have been granted.
REQUIRED_SCOPES = "user-read-playback-state user-modify-playback-state"


class SpotifyWebApi(SpotifyService):
    name = "spotify-web-api"

    def __init__(
        self,
        *,
        client_id: str | None = None,
        client_secret: str | None = None,
        refresh_token: str | None = None,
        http: Any = None,
        timeout_s: float = 10.0,
    ) -> None:
        self._client_id = client_id or os.environ.get("SPOTIFY_CLIENT_ID", "")
        self._client_secret = client_secret or os.environ.get(
            "SPOTIFY_CLIENT_SECRET", ""
        )
        self._refresh_token = refresh_token or os.environ.get(
            "SPOTIFY_REFRESH_TOKEN", ""
        )

        if not (self._client_id and self._client_secret and self._refresh_token):
            raise SpotifyNotConfigured(
                "Spotify needs SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET and "
                "SPOTIFY_REFRESH_TOKEN in the environment. See .env.example. "
                "Use the fake service for offline work."
            )

        self._timeout_s = timeout_s
        self._access_token = ""
        self._expires_at = 0.0

        if http is not None:
            # Injected for tests: no network, no credentials
            # ever leave the process.
            self._http = http
        else:
            import httpx2

            self._http = httpx2.AsyncClient(timeout=timeout_s)

    # -- auth ---------------------------------------------

    async def _token(self) -> str:
        """Current access token, refreshed when near expiry."""
        if self._access_token and time.time() < self._expires_at - 30:
            return self._access_token

        basic = base64.b64encode(
            f"{self._client_id}:{self._client_secret}".encode()
        ).decode()

        response = await self._http.post(
            TOKEN_URL,
            headers={"Authorization": f"Basic {basic}"},
            data={
                "grant_type": "refresh_token",
                "refresh_token": self._refresh_token,
            },
        )

        if response.status_code in (400, 401):
            raise SpotifyAuthError(
                "Spotify rejected the credentials. The refresh token may have "
                "been revoked; re-run the authorisation flow."
            )
        if response.status_code != 200:
            raise SpotifyApiError(response.status_code, "token exchange failed")

        payload = response.json()
        self._access_token = payload["access_token"]
        self._expires_at = time.time() + int(payload.get("expires_in", 3600))

        # Deliberately not logged, not even truncated.
        log.info("refreshed the Spotify access token")
        return self._access_token

    async def _request(
        self, method: str, path: str, **kwargs: Any
    ) -> tuple[int, dict]:
        """One API call, with the error taxonomy applied.

        `path` is always a literal from this module -- it is
        never built from model output.
        """
        token = await self._token()

        response = await self._http.request(
            method,
            f"{API}{path}",
            headers={"Authorization": f"Bearer {token}"},
            **kwargs,
        )

        status = response.status_code

        if status == 401:
            # Token went stale mid-flight; drop it so the
            # next call refreshes rather than looping.
            self._access_token = ""
            raise SpotifyAuthError("Spotify access token was rejected")

        if status == 403:
            raise SpotifyPremiumRequired()

        if status == 429:
            retry = int(response.headers.get("Retry-After", "1"))
            raise SpotifyRateLimited(retry)

        if status == 404:
            raise SpotifyNoActiveDevice()

        if status in (204, 202):
            return status, {}

        if status >= 400:
            raise SpotifyApiError(status, str(response.text)[:120])

        try:
            return status, response.json()
        except Exception:  # noqa: BLE001
            return status, {}

    # -- parsing ------------------------------------------

    @staticmethod
    def _to_track(item: dict) -> Track | None:
        if not item or not item.get("id"):
            return None

        artists = item.get("artists") or []
        return Track(
            id=item["id"],
            name=item.get("name", ""),
            artist=artists[0].get("name", "") if artists else "",
            album=(item.get("album") or {}).get("name", ""),
            duration_ms=int(item.get("duration_ms") or 0),
        )

    def _to_state(self, payload: dict) -> PlaybackState:
        if not payload:
            return PlaybackState(status=SpotifyStatus.IDLE)

        track = self._to_track(payload.get("item") or {})
        if track is None:
            return PlaybackState(status=SpotifyStatus.IDLE)

        return PlaybackState(
            status=(
                SpotifyStatus.PLAYING
                if payload.get("is_playing")
                else SpotifyStatus.PAUSED
            ),
            track=track,
            progress_ms=int(payload.get("progress_ms") or 0),
            device=(payload.get("device") or {}).get("name"),
        )

    # -- interface ----------------------------------------

    async def now_playing(self) -> PlaybackState:
        try:
            _, payload = await self._request("GET", "/me/player")
        except SpotifyNoActiveDevice:
            # Nothing open is not an error for a read.
            return PlaybackState(status=SpotifyStatus.IDLE)

        return self._to_state(payload)

    async def resume(self) -> PlaybackState:
        await self._request("PUT", "/me/player/play")
        return await self.now_playing()

    async def pause(self) -> PlaybackState:
        await self._request("PUT", "/me/player/pause")
        return await self.now_playing()

    async def next_track(self) -> PlaybackState:
        await self._request("POST", "/me/player/next")
        return await self.now_playing()

    async def previous_track(self) -> PlaybackState:
        await self._request("POST", "/me/player/previous")
        return await self.now_playing()

    async def search(self, query: str, kind: str, limit: int = 5) -> list[SearchHit]:
        query = validate_query(query)
        kind = validate_search_type(kind)
        limit = max(1, min(int(limit), 20))

        _, payload = await self._request(
            "GET",
            "/search",
            params={"q": query, "type": kind, "limit": limit},
        )

        items = (payload.get(f"{kind}s") or {}).get("items") or []
        hits: list[SearchHit] = []

        for item in items:
            if not item or not item.get("id"):
                continue

            if kind == "track":
                artists = item.get("artists") or []
                owner = artists[0].get("name", "") if artists else ""
            elif kind == "album":
                artists = item.get("artists") or []
                owner = artists[0].get("name", "") if artists else ""
            elif kind == "playlist":
                owner = (item.get("owner") or {}).get("display_name", "")
            else:
                owner = ""

            hits.append(
                SearchHit(
                    id=item["id"],
                    name=item.get("name", ""),
                    kind=kind,
                    owner=owner,
                )
            )

        return hits

    async def _start(self, body: dict) -> PlaybackState:
        await self._request("PUT", "/me/player/play", json=body)
        return await self.now_playing()

    async def play_track(self, track_id: str) -> PlaybackState:
        track_id = validate_id(track_id, "track")
        return await self._start({"uris": [f"spotify:track:{track_id}"]})

    async def play_album(self, album_id: str) -> PlaybackState:
        album_id = validate_id(album_id, "album")
        return await self._start({"context_uri": f"spotify:album:{album_id}"})

    async def play_playlist(self, playlist_id: str) -> PlaybackState:
        playlist_id = validate_id(playlist_id, "playlist")
        return await self._start({"context_uri": f"spotify:playlist:{playlist_id}"})


def make_spotify_service(name: str | None = None) -> SpotifyService:
    """Pick a service from the environment.

    Defaults to the fake, so nothing tries to reach Spotify
    unless it was asked to.
    """
    from .service import FakeSpotifyService

    choice = (name or os.environ.get("SPOTIFY_SERVICE", "fake")).lower().strip()

    if choice == "fake":
        return FakeSpotifyService()
    if choice in ("web", "real", "spotify"):
        return SpotifyWebApi()

    raise SpotifyNotConfigured(
        f"unknown SPOTIFY_SERVICE {choice!r}; valid: fake, web"
    )
