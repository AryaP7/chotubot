"""Spotify integration.

    Agent -> ToolRegistry -> Spotify tools -> SpotifyService -> Web API
                                    |
                              BotGateway -> Protocol v1 -> ESP32

The Agent has no Spotify knowledge; the ESP32 has no
Spotify credentials. Both only ever see the normalized
`spotify_state` message.
"""

from .models import (
    PlaybackState,
    SearchHit,
    SpotifyApiError,
    SpotifyAuthError,
    SpotifyError,
    SpotifyNoActiveDevice,
    SpotifyNotConfigured,
    SpotifyNotFound,
    SpotifyPremiumRequired,
    SpotifyRateLimited,
    SpotifyStatus,
    Track,
    validate_id,
    validate_query,
    validate_search_type,
)
from .service import FakeSpotifyService, SpotifyService
from .tools import spotify_tools
from .web_api import SpotifyWebApi, make_spotify_service

__all__ = [
    "PlaybackState",
    "SearchHit",
    "Track",
    "SpotifyStatus",
    "SpotifyService",
    "FakeSpotifyService",
    "SpotifyWebApi",
    "make_spotify_service",
    "spotify_tools",
    "SpotifyError",
    "SpotifyNotConfigured",
    "SpotifyAuthError",
    "SpotifyRateLimited",
    "SpotifyNoActiveDevice",
    "SpotifyPremiumRequired",
    "SpotifyNotFound",
    "SpotifyApiError",
    "validate_id",
    "validate_query",
    "validate_search_type",
]
