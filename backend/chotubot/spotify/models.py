"""Spotify domain types, validation and errors.

Everything the LLM can influence is validated here. The
model never supplies a URL, an endpoint, or a raw URI --
it supplies a search phrase or an ID, and an ID that is
not exactly 22 base62 characters is rejected before any
request is built.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from enum import Enum
from typing import Any

# Spotify IDs are 22 base62 characters. Anchored, so a
# crafted string cannot smuggle a path or query separator
# into a URL.
_ID_PATTERN = re.compile(r"^[A-Za-z0-9]{22}$")

_URI_PATTERN = re.compile(r"^spotify:(track|album|playlist|artist):[A-Za-z0-9]{22}$")

SEARCH_TYPES = frozenset({"track", "album", "playlist", "artist"})

# The maximum a search phrase may be. Long enough for any
# real request, short enough to bound what we forward.
MAX_QUERY_CHARS = 120


class SpotifyStatus(str, Enum):
    """Mirrors the firmware's SpotifyStatus enum."""

    IDLE = "idle"
    PLAYING = "playing"
    PAUSED = "paused"
    LOADING = "loading"
    ERROR = "error"


# --- errors ----------------------------------------------


class SpotifyError(Exception):
    """Base for everything this package raises."""


class SpotifyNotConfigured(SpotifyError):
    """Credentials are absent or incomplete."""


class SpotifyAuthError(SpotifyError):
    """Spotify rejected the credentials."""


class SpotifyRateLimited(SpotifyError):
    def __init__(self, retry_after_s: int = 1) -> None:
        super().__init__(f"rate limited by Spotify; retry in {retry_after_s}s")
        self.retry_after_s = retry_after_s


class SpotifyNoActiveDevice(SpotifyError):
    """Nothing is available to play on.

    Spotify has no concept of "just start playing" without a
    device -- something must already be open somewhere.
    """

    def __init__(self) -> None:
        super().__init__(
            "no active Spotify device. Open Spotify on a phone, desktop or "
            "speaker and start playing something once, then try again."
        )


class SpotifyPremiumRequired(SpotifyError):
    def __init__(self) -> None:
        super().__init__("playback control requires a Spotify Premium account")


class SpotifyNotFound(SpotifyError):
    """Nothing matched the search."""


class SpotifyApiError(SpotifyError):
    def __init__(self, status: int, detail: str = "") -> None:
        super().__init__(f"Spotify API returned {status}: {detail}"[:200])
        self.status = status


# --- validation ------------------------------------------


def validate_id(value: str, kind: str = "item") -> str:
    if not isinstance(value, str):
        raise SpotifyError(f"{kind} id must be a string")

    value = value.strip()

    # Accept a full URI and take the id out of it.
    if _URI_PATTERN.match(value):
        return value.rsplit(":", 1)[1]

    if not _ID_PATTERN.match(value):
        raise SpotifyError(
            f"{value!r} is not a valid Spotify {kind} id "
            "(22 letters and digits)"
        )

    return value


def validate_query(value: str) -> str:
    if not isinstance(value, str):
        raise SpotifyError("search text must be a string")

    value = value.strip()
    if not value:
        raise SpotifyError("search text must not be empty")

    if len(value) > MAX_QUERY_CHARS:
        raise SpotifyError(f"search text must be under {MAX_QUERY_CHARS} characters")

    return value


def validate_search_type(value: str) -> str:
    value = str(value).lower().strip()
    if value not in SEARCH_TYPES:
        raise SpotifyError(
            f"unknown search type {value!r}; valid: {', '.join(sorted(SEARCH_TYPES))}"
        )
    return value


def validate_volume(value: Any) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise SpotifyError("volume must be a whole number")
    if not 0 <= value <= 100:
        raise SpotifyError("volume must be between 0 and 100")
    return value


# --- domain ----------------------------------------------


@dataclass(frozen=True)
class Track:
    id: str
    name: str
    artist: str
    album: str = ""
    duration_ms: int = 0

    @property
    def uri(self) -> str:
        return f"spotify:track:{self.id}"

    def describe(self) -> str:
        return f"{self.name} by {self.artist}" if self.artist else self.name


@dataclass(frozen=True)
class SearchHit:
    id: str
    name: str
    kind: str
    owner: str = ""

    @property
    def uri(self) -> str:
        return f"spotify:{self.kind}:{self.id}"

    def describe(self) -> str:
        return f"{self.name} ({self.owner})" if self.owner else self.name


@dataclass
class PlaybackState:
    status: SpotifyStatus = SpotifyStatus.IDLE
    track: Track | None = None
    progress_ms: int = 0
    device: str | None = None

    @property
    def playing(self) -> bool:
        return self.status is SpotifyStatus.PLAYING

    def describe(self) -> str:
        if self.status is SpotifyStatus.ERROR:
            return "Spotify is not available right now."
        if self.status is SpotifyStatus.IDLE or self.track is None:
            return "Nothing is playing on Spotify."

        verb = "Playing" if self.playing else "Paused"
        line = f"{verb}: {self.track.describe()}"
        if self.track.album:
            line += f", from {self.track.album}"
        return line + "."

    def to_protocol_kwargs(self) -> dict[str, Any]:
        """Shape the ESP32's Protocol v1 spotify_state expects.

        Deliberately narrow -- the bot gets what it can show
        on a 128x64 display and nothing else. No ids, no
        URIs, no tokens, no device identifiers.
        """
        return {
            "playing": self.playing,
            "track": self.track.name if self.track else "",
            "artist": self.track.artist if self.track else "",
            "progress_ms": max(0, self.progress_ms),
            "duration_ms": self.track.duration_ms if self.track else 0,
            "status": self.status.value,
        }
