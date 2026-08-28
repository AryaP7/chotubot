"""Spotify tools for the Agent.

Each tool is a thin shell: validate, call the service,
push the resulting state to the bot, return a sentence.
No Spotify API knowledge lives here, and none of it lives
in the Agent.

Every tool that changes playback also sends the normalized
Protocol v1 `spotify_state` through BotGateway, so the OLED
follows whatever the model just did without the model
knowing the bot exists.
"""

from __future__ import annotations

import logging

from ..agent.gateway import BotGateway
from ..agent.tools import Tool, ToolParameter, ToolResult
from .models import (
    SpotifyError,
    SpotifyNoActiveDevice,
    SpotifyNotFound,
    SpotifyPremiumRequired,
    SpotifyRateLimited,
    SpotifyStatus,
    PlaybackState,
)
from .service import SpotifyService

log = logging.getLogger("chotubot.spotify")


class _SpotifyTool(Tool):
    """Shared plumbing for every Spotify tool."""

    def __init__(self, service: SpotifyService, gateway: BotGateway) -> None:
        self._service = service
        self._gateway = gateway

    async def _publish(self, state: PlaybackState) -> None:
        """Mirror playback onto the bot.

        Failing to reach the bot must never fail the tool --
        the music is playing either way.
        """
        try:
            await self._gateway.send_spotify_state(**state.to_protocol_kwargs())
        except Exception as exc:  # noqa: BLE001
            log.warning("could not push spotify state to the bot: %s", exc)

    async def _run_guarded(self, action, *args) -> ToolResult:
        """Turn the error taxonomy into readable results.

        A tool result is what the model reads next, so each
        message says what to do about it.
        """
        try:
            state = await action(*args)
        except SpotifyNoActiveDevice as exc:
            return ToolResult.failure(str(exc))
        except SpotifyPremiumRequired as exc:
            return ToolResult.failure(str(exc))
        except SpotifyRateLimited as exc:
            return ToolResult.failure(str(exc))
        except SpotifyNotFound as exc:
            return ToolResult.failure(str(exc))
        except SpotifyError as exc:
            return ToolResult.failure(str(exc))
        except Exception as exc:  # noqa: BLE001
            return ToolResult.failure(f"{type(exc).__name__}: {exc}")

        await self._publish(state)
        return ToolResult.success(state.describe())


class SpotifyNowPlayingTool(_SpotifyTool):
    name = "spotify_now_playing"
    description = (
        "Find out what is currently playing on Spotify: track, artist and album."
    )
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        return await self._run_guarded(self._service.now_playing)


class SpotifyPlayTool(_SpotifyTool):
    name = "spotify_play"
    description = (
        "Start or resume Spotify playback of whatever is already queued. "
        "To play something specific, use one of the spotify_play_* tools."
    )
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        return await self._run_guarded(self._service.resume)


class SpotifyResumeTool(_SpotifyTool):
    name = "spotify_resume"
    description = "Resume Spotify after it was paused."
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        return await self._run_guarded(self._service.resume)


class SpotifyPauseTool(_SpotifyTool):
    name = "spotify_pause"
    description = "Pause Spotify playback."
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        return await self._run_guarded(self._service.pause)


class SpotifyNextTool(_SpotifyTool):
    name = "spotify_next"
    description = "Skip to the next track on Spotify."
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        return await self._run_guarded(self._service.next_track)


class SpotifyPreviousTool(_SpotifyTool):
    name = "spotify_previous"
    description = "Go back to the previous track on Spotify."
    parameters: list[ToolParameter] = []

    async def run(self) -> ToolResult:
        return await self._run_guarded(self._service.previous_track)


class SpotifySearchTool(_SpotifyTool):
    name = "spotify_search"
    description = (
        "Search Spotify without playing anything. Useful for checking what "
        "exists before choosing something to play."
    )
    parameters = [
        ToolParameter(name="query", description="What to search for."),
        ToolParameter(
            name="kind",
            description="What sort of thing to look for.",
            enum=["track", "album", "playlist", "artist"],
        ),
    ]

    async def run(self, query: str, kind: str) -> ToolResult:
        try:
            hits = await self._service.search(query, kind, limit=5)
        except SpotifyError as exc:
            return ToolResult.failure(str(exc))
        except Exception as exc:  # noqa: BLE001
            return ToolResult.failure(f"{type(exc).__name__}: {exc}")

        if not hits:
            return ToolResult.success(f"No {kind} found matching {query!r}.")

        listed = "; ".join(hit.describe() for hit in hits)
        return ToolResult.success(f"Found: {listed}.")


class SpotifyPlayTrackTool(_SpotifyTool):
    name = "spotify_play_track"
    description = (
        "Play a specific song on Spotify. Give the song name as the user "
        "said it -- it is looked up automatically."
    )
    parameters = [
        ToolParameter(name="query", description="The song to play, e.g. 'Get Lucky'.")
    ]

    async def run(self, query: str) -> ToolResult:
        return await self._run_guarded(self._service.find_and_play, query, "track")


class SpotifyPlayAlbumTool(_SpotifyTool):
    name = "spotify_play_album"
    description = "Play a whole album on Spotify, looked up by name."
    parameters = [ToolParameter(name="query", description="The album to play.")]

    async def run(self, query: str) -> ToolResult:
        return await self._run_guarded(self._service.find_and_play, query, "album")


class SpotifyPlayPlaylistTool(_SpotifyTool):
    name = "spotify_play_playlist"
    description = (
        "Play a playlist on Spotify, looked up by name, e.g. 'my coding playlist'."
    )
    parameters = [ToolParameter(name="query", description="The playlist to play.")]

    async def run(self, query: str) -> ToolResult:
        return await self._run_guarded(self._service.find_and_play, query, "playlist")


def spotify_tools(service: SpotifyService, gateway: BotGateway) -> list[Tool]:
    """Every Spotify tool, ready to register."""
    return [
        SpotifyNowPlayingTool(service, gateway),
        SpotifyPlayTool(service, gateway),
        SpotifyResumeTool(service, gateway),
        SpotifyPauseTool(service, gateway),
        SpotifyNextTool(service, gateway),
        SpotifyPreviousTool(service, gateway),
        SpotifySearchTool(service, gateway),
        SpotifyPlayTrackTool(service, gateway),
        SpotifyPlayAlbumTool(service, gateway),
        SpotifyPlayPlaylistTool(service, gateway),
    ]
