"""Validation rules, tested without a socket.

These are the rules that stop a malformed message making a
physical object do something unintended, so they get tested
directly rather than only through the server.
"""

from __future__ import annotations

import pytest

from chotubot import protocol
from chotubot.protocol import ProtocolError


class TestInboundParsing:
    def test_hello_requires_token(self):
        with pytest.raises(ProtocolError, match="token"):
            protocol.parse_inbound({"type": "hello"})

    def test_hello_defaults_to_v1(self):
        parsed = protocol.parse_inbound({"type": "hello", "token": "x"})
        assert parsed["v"] == 1

    def test_hello_rejects_non_integer_version(self):
        with pytest.raises(ProtocolError, match="'v'"):
            protocol.parse_inbound({"type": "hello", "token": "x", "v": "one"})

    def test_bool_is_not_an_integer_version(self):
        # True == 1 in Python; the parser must not accept it.
        with pytest.raises(ProtocolError):
            protocol.parse_inbound({"type": "hello", "token": "x", "v": True})

    def test_unknown_type_rejected(self):
        with pytest.raises(ProtocolError, match="unknown message type"):
            protocol.parse_inbound({"type": "launch_missiles"})

    def test_non_object_rejected(self):
        with pytest.raises(ProtocolError, match="JSON object"):
            protocol.parse_inbound(["not", "a", "dict"])

    def test_missing_type_rejected(self):
        with pytest.raises(ProtocolError, match="missing 'type'"):
            protocol.parse_inbound({"token": "x"})

    def test_known_event_accepted(self):
        parsed = protocol.parse_inbound({"type": "event", "event": "TOUCH_SHORT"})
        assert parsed["event"] == "TOUCH_SHORT"

    def test_unknown_event_rejected(self):
        with pytest.raises(ProtocolError, match="unknown event"):
            protocol.parse_inbound({"type": "event", "event": "SELF_DESTRUCT"})

    def test_oversized_token_rejected(self):
        with pytest.raises(ProtocolError, match="exceeds"):
            protocol.parse_inbound({"type": "hello", "token": "x" * 200})


class TestOutboundBuilders:
    def test_expression_normalises_case(self):
        assert protocol.expression("HAPPY")["value"] == "happy"
        assert protocol.expression("  Happy  ")["value"] == "happy"

    def test_unknown_expression_rejected(self):
        with pytest.raises(ProtocolError, match="unknown expression"):
            protocol.expression("wizard")

    def test_every_expression_is_buildable(self):
        # Guards against the set and the builder drifting.
        for name in protocol.EXPRESSIONS:
            assert protocol.expression(name)["value"] == name

    def test_unknown_state_rejected(self):
        with pytest.raises(ProtocolError, match="unknown state"):
            protocol.state("DANCING")

    def test_notification_truncated_to_firmware_buffers(self):
        # Firmware buffers are char[28] and char[40].
        built = protocol.notification("T" * 100, "B" * 100)
        assert len(built["title"]) == 27
        assert len(built["body"]) == 39

    def test_spotify_clamps_negative_progress(self):
        built = protocol.spotify_state(playing=True, progress_ms=-5)
        assert built["progress_ms"] == 0

    def test_error_message_truncated(self):
        assert len(protocol.error("code", "x" * 500)["message"]) == 120
