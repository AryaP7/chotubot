"""SpotifyService: the interface, and an offline fake.

The Agent never touches this directly -- tools do, and the
tools go through this interface, so the whole feature is
testable with no account, no credentials and no network.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from .models import (
    PlaybackState,
    SearchHit,
    SpotifyNoActiveDevice,
    SpotifyNotFound,
    SpotifyStatus,
    Track,
    validate_id,
    validate_query,
    validate_search_type,
)


class SpotifyService(ABC):
    """Everything the bot can ask Spotify to do.

    Only operations the Authorization Code flow actually
    supports are declared. There is no "create playlist",
    no "follow artist" -- adding one means adding a tool
    for it deliberately.
    """

    name: str = "abstract"

    @abstractmethod
    async def now_playing(self) -> PlaybackState: ...

    @abstractmethod
    async def resume(self) -> PlaybackState: ...

    @abstractmethod
    async def pause(self) -> PlaybackState: ...

    @abstractmethod
    async def next_track(self) -> PlaybackState: ...

    @abstractmethod
    async def previous_track(self) -> PlaybackState: ...

    @abstractmethod
    async def search(self, query: str, kind: str, limit: int = 5) -> list[SearchHit]: ...

    @abstractmethod
    async def play_track(self, track_id: str) -> PlaybackState: ...

    @abstractmethod
    async def play_album(self, album_id: str) -> PlaybackState: ...

    @abstractmethod
    async def play_playlist(self, playlist_id: str) -> PlaybackState: ...

    # -- shared helper ------------------------------------

    async def find_and_play(self, query: str, kind: str) -> PlaybackState:
        """Resolve a human phrase, then play the best match.

        This is what lets "play my coding playlist" work
        without the model ever handling an id.
        """
        query = validate_query(query)
        kind = validate_search_type(kind)

        if kind == "artist":
            # There is no "play an artist" endpoint; play
            # their most relevant track instead.
            hits = await self.search(query, "track", limit=1)
            if not hits:
                raise SpotifyNotFound(f"nothing found for {query!r}")
            return await self.play_track(hits[0].id)

        hits = await self.search(query, kind, limit=1)
        if not hits:
            raise SpotifyNotFound(f"no {kind} found for {query!r}")

        if kind == "track":
            return await self.play_track(hits[0].id)
        if kind == "album":
            return await self.play_album(hits[0].id)
        return await self.play_playlist(hits[0].id)


class FakeSpotifyService(SpotifyService):
    """Deterministic in-memory Spotify.

    Small fixed library, predictable ordering, no clock and
    no randomness. Failure modes are switchable so the tool
    layer's error handling can be exercised.
    """

    name = "fake"

    LIBRARY: dict[str, list[SearchHit]] = {
        "track": [
            SearchHit("4Dvkj6JhhA12EX05fT7y2e", "As It Was", "track", "Harry Styles"),
            SearchHit("0VjIjW4GlUZAMYd2vXMi3b", "Blinding Lights", "track", "The Weeknd"),
            SearchHit("2Foc5Q5nqNiosCNqttzHof", "Get Lucky", "track", "Daft Punk"),
            SearchHit("5W3cjX2J3tjhG8zb6u0qHn", "Around the World", "track", "Daft Punk"),
        ],
        "album": [
            SearchHit("4m2880jivSbbyEGAKfITCa", "Random Access Memories", "album", "Daft Punk"),
            SearchHit("2nLOHgzXzwFEpl62zAgCEC", "After Hours", "album", "The Weeknd"),
        ],
        "playlist": [
            SearchHit("37i9dQZF1DX5trt9i14X7j", "Coding Mode", "playlist", "Spotify"),
            SearchHit("37i9dQZF1DXcBWIGoYBM5M", "Today's Top Hits", "playlist", "Spotify"),
        ],
        "artist": [
            SearchHit("4tZwfgrHOc3mvqYlEYSvVi", "Daft Punk", "artist", ""),
        ],
    }

    TRACKS: dict[str, Track] = {
        "4Dvkj6JhhA12EX05fT7y2e": Track(
            "4Dvkj6JhhA12EX05fT7y2e", "As It Was", "Harry Styles", "Harry's House", 167000
        ),
        "0VjIjW4GlUZAMYd2vXMi3b": Track(
            "0VjIjW4GlUZAMYd2vXMi3b", "Blinding Lights", "The Weeknd", "After Hours", 200000
        ),
        "2Foc5Q5nqNiosCNqttzHof": Track(
            "2Foc5Q5nqNiosCNqttzHof", "Get Lucky", "Daft Punk", "Random Access Memories", 248000
        ),
        "5W3cjX2J3tjhG8zb6u0qHn": Track(
            "5W3cjX2J3tjhG8zb6u0qHn", "Around the World", "Daft Punk", "Homework", 428000
        ),
    }

    def __init__(self, *, has_device: bool = True) -> None:
        self.state = PlaybackState()
        self.has_device = has_device
        self.calls: list[str] = []

        # Ordered queue used by next/previous.
        self._queue = list(self.TRACKS.values())
        self._index = 0

    # -- test controls ------------------------------------

    def set_playing(self, track_id: str, progress_ms: int = 0) -> None:
        track = self.TRACKS[track_id]
        self._index = self._queue.index(track)
        self.state = PlaybackState(
            status=SpotifyStatus.PLAYING,
            track=track,
            progress_ms=progress_ms,
            device="Fake Speaker",
        )

    def _require_device(self) -> None:
        if not self.has_device:
            raise SpotifyNoActiveDevice()

    def _play_current(self) -> PlaybackState:
        self.state = PlaybackState(
            status=SpotifyStatus.PLAYING,
            track=self._queue[self._index],
            progress_ms=0,
            device="Fake Speaker",
        )
        return self.state

    # -- interface ----------------------------------------

    async def now_playing(self) -> PlaybackState:
        self.calls.append("now_playing")
        return self.state

    async def resume(self) -> PlaybackState:
        self.calls.append("resume")
        self._require_device()

        if self.state.track is None:
            return self._play_current()

        self.state.status = SpotifyStatus.PLAYING
        return self.state

    async def pause(self) -> PlaybackState:
        self.calls.append("pause")
        self._require_device()

        if self.state.track is not None:
            self.state.status = SpotifyStatus.PAUSED
        return self.state

    async def next_track(self) -> PlaybackState:
        self.calls.append("next_track")
        self._require_device()

        self._index = (self._index + 1) % len(self._queue)
        return self._play_current()

    async def previous_track(self) -> PlaybackState:
        self.calls.append("previous_track")
        self._require_device()

        self._index = (self._index - 1) % len(self._queue)
        return self._play_current()

    async def search(self, query: str, kind: str, limit: int = 5) -> list[SearchHit]:
        self.calls.append(f"search:{kind}")

        query = validate_query(query).lower()
        kind = validate_search_type(kind)

        hits = [
            hit
            for hit in self.LIBRARY.get(kind, [])
            if query in hit.name.lower() or query in hit.owner.lower()
        ]
        return hits[:limit]

    async def play_track(self, track_id: str) -> PlaybackState:
        self.calls.append("play_track")
        track_id = validate_id(track_id, "track")
        self._require_device()

        track = self.TRACKS.get(track_id)
        if track is None:
            raise SpotifyNotFound(f"no track with id {track_id}")

        self._index = self._queue.index(track)
        return self._play_current()

    async def play_album(self, album_id: str) -> PlaybackState:
        self.calls.append("play_album")
        album_id = validate_id(album_id, "album")
        self._require_device()

        # Albums start at their first track in this fake.
        self._index = 0
        return self._play_current()

    async def play_playlist(self, playlist_id: str) -> PlaybackState:
        self.calls.append("play_playlist")
        playlist_id = validate_id(playlist_id, "playlist")
        self._require_device()

        self._index = 0
        return self._play_current()
