// MiniWebSocket - the hand-written RFC 6455 client.
//
// The riskiest code in the firmware, because it is the only
// protocol implementation not backed by a library. The
// socket is an in-memory loopback, so the real handshake,
// masking and frame parsing execute unchanged.

#include "support/fixture.h"
#include "support/tinytest.h"

#include "src/net/MiniWebSocket.h"

namespace {

std::vector<std::pair<WsEvent, std::string>> g_received;

void onEvent(WsEvent event, const uint8_t* payload, size_t length) {
  g_received.push_back(
      {event, payload ? std::string(reinterpret_cast<const char*>(payload),
                                    length)
                      : std::string()});
}

struct Rig {
  MiniWebSocket ws;

  Rig() {
    hostsim::resetAll();
    g_received.clear();
    ws.onEvent(onEvent);
  }

  // Connect and complete a successful handshake.
  void open() {
    ws.begin("127.0.0.1", 8080, "/bot");
    ws.loop(millis());   // sends the GET

    hostsim::netQueue(
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "\r\n");

    hostsim::advanceMillis(10);
    ws.loop(millis());
  }

  // Build an unmasked server->client frame.
  static std::string frame(uint8_t opcode, const std::string& payload) {
    std::string out;
    out.push_back(static_cast<char>(0x80 | opcode));

    if (payload.size() < 126) {
      out.push_back(static_cast<char>(payload.size()));
    } else {
      out.push_back(static_cast<char>(126));
      out.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
      out.push_back(static_cast<char>(payload.size() & 0xFF));
    }

    out += payload;
    return out;
  }

  int countOf(WsEvent wanted) {
    int seen = 0;
    for (const auto& entry : g_received) {
      if (entry.first == wanted) ++seen;
    }
    return seen;
  }
};

}  // namespace

TEST(WebSocket, handshake_sends_a_valid_upgrade_request) {
  Rig rig;
  rig.ws.begin("192.168.1.50", 8080, "/bot");
  rig.ws.loop(millis());

  const std::string& sent = hostsim::netSent();

  CHECK(sent.find("GET /bot HTTP/1.1") != std::string::npos);
  CHECK(sent.find("Host: 192.168.1.50:8080") != std::string::npos);
  CHECK(sent.find("Upgrade: websocket") != std::string::npos);
  CHECK(sent.find("Connection: Upgrade") != std::string::npos);
  CHECK(sent.find("Sec-WebSocket-Version: 13") != std::string::npos);
  CHECK(sent.find("Sec-WebSocket-Key: ") != std::string::npos);
  CHECK(sent.find("\r\n\r\n") != std::string::npos);
}

TEST(WebSocket, nonce_is_base64_of_the_right_length) {
  Rig rig;
  rig.ws.begin("h", 1, "/x");
  rig.ws.loop(millis());

  const std::string& sent = hostsim::netSent();
  const size_t start = sent.find("Sec-WebSocket-Key: ") + 19;
  const size_t end = sent.find("\r\n", start);
  const std::string key = sent.substr(start, end - start);

  // 16 random bytes -> 24 base64 characters ending in "==".
  CHECK_EQ(key.size(), 24u);
  CHECK_EQ(key.substr(22), std::string("=="));
}

TEST(WebSocket, http_101_opens_the_connection) {
  Rig rig;
  rig.open();

  CHECK(rig.ws.isConnected());
  CHECK_EQ(rig.countOf(WsEvent::Connected), 1);
}

TEST(WebSocket, non_101_response_is_refused) {
  Rig rig;
  rig.ws.begin("h", 1, "/x");
  rig.ws.loop(millis());

  hostsim::netQueue("HTTP/1.1 403 Forbidden\r\n\r\n");
  hostsim::advanceMillis(10);
  rig.ws.loop(millis());

  CHECK(!rig.ws.isConnected());
}

TEST(WebSocket, failed_tcp_connect_does_not_open) {
  hostsim::resetAll();
  hostsim::setNetConnectOk(false);

  MiniWebSocket ws;
  ws.begin("h", 1, "/x");
  ws.loop(millis());

  CHECK(!ws.isConnected());
}

TEST(WebSocket, receives_a_text_frame) {
  Rig rig;
  rig.open();

  hostsim::netQueue(Rig::frame(0x1, "{\"type\":\"pong\"}"));
  rig.ws.loop(millis());

  CHECK_EQ(rig.countOf(WsEvent::Text), 1);
  for (const auto& entry : g_received) {
    if (entry.first == WsEvent::Text) {
      CHECK_EQ(entry.second, std::string("{\"type\":\"pong\"}"));
    }
  }
}

TEST(WebSocket, receives_a_binary_frame) {
  Rig rig;
  rig.open();

  hostsim::netQueue(Rig::frame(0x2, std::string("\x01\x02\x03\x04", 4)));
  rig.ws.loop(millis());

  CHECK_EQ(rig.countOf(WsEvent::Binary), 1);
}

TEST(WebSocket, handles_a_two_byte_length_frame) {
  Rig rig;
  rig.open();

  const std::string payload(300, 'x');
  hostsim::netQueue(Rig::frame(0x2, payload));
  rig.ws.loop(millis());

  size_t total = 0;
  for (const auto& entry : g_received) {
    if (entry.first == WsEvent::Binary) total += entry.second.size();
  }
  CHECK_EQ(total, payload.size());
}

TEST(WebSocket, oversized_text_frame_is_dropped_not_truncated) {
  Rig rig;
  rig.open();

  // Larger than kRxBufferSize: half a JSON object is worse
  // than none, so the firmware drops it.
  const std::string payload(MiniWebSocket::kRxBufferSize + 200, 'y');
  hostsim::netQueue(Rig::frame(0x1, payload));
  rig.ws.loop(millis());

  CHECK_EQ(rig.countOf(WsEvent::Text), 0);
}

TEST(WebSocket, ping_is_answered_with_a_pong) {
  Rig rig;
  rig.open();
  hostsim::netClearSent();

  hostsim::netQueue(Rig::frame(0x9, "hi"));
  rig.ws.loop(millis());

  const std::string& sent = hostsim::netSent();
  CHECK(sent.size() >= 2);
  CHECK_EQ(static_cast<uint8_t>(sent[0]) & 0x0F, 0x0A);   // pong opcode
}

TEST(WebSocket, close_frame_disconnects) {
  Rig rig;
  rig.open();

  hostsim::netQueue(Rig::frame(0x8, ""));
  rig.ws.loop(millis());

  CHECK(!rig.ws.isConnected());
  CHECK_EQ(rig.countOf(WsEvent::Disconnected), 1);
}

TEST(WebSocket, sent_frames_are_masked_as_the_spec_requires) {
  Rig rig;
  rig.open();
  hostsim::netClearSent();

  CHECK(rig.ws.sendText("hello"));

  const std::string& sent = hostsim::netSent();
  CHECK_EQ(static_cast<uint8_t>(sent[0]), 0x81);           // FIN + text

  const uint8_t second = static_cast<uint8_t>(sent[1]);
  CHECK_EQ(second & 0x80, 0x80);                            // MASK bit set
  CHECK_EQ(second & 0x7F, 5);                               // length

  // Unmask and confirm the payload survived the round trip.
  const uint8_t* mask = reinterpret_cast<const uint8_t*>(sent.data()) + 2;
  std::string decoded;
  for (size_t i = 0; i < 5; ++i) {
    decoded.push_back(static_cast<char>(sent[6 + i] ^ mask[i & 3]));
  }
  CHECK_EQ(decoded, std::string("hello"));
}

TEST(WebSocket, long_payload_uses_the_extended_length_field) {
  Rig rig;
  rig.open();
  hostsim::netClearSent();

  std::vector<uint8_t> payload(500, 0x5A);
  CHECK(rig.ws.sendBinary(payload.data(), payload.size()));

  const std::string& sent = hostsim::netSent();
  CHECK_EQ(static_cast<uint8_t>(sent[0]), 0x82);            // FIN + binary
  CHECK_EQ(static_cast<uint8_t>(sent[1]) & 0x7F, 126);      // extended
  const uint16_t length = (static_cast<uint8_t>(sent[2]) << 8) |
                          static_cast<uint8_t>(sent[3]);
  CHECK_EQ(length, 500);
}

TEST(WebSocket, sending_before_open_is_refused) {
  hostsim::resetAll();
  MiniWebSocket ws;

  CHECK(!ws.sendText("nope"));
  CHECK(!ws.sendBinary(reinterpret_cast<const uint8_t*>("x"), 1));
}

TEST(WebSocket, dropped_connection_is_noticed) {
  Rig rig;
  rig.open();

  hostsim::dropConnection();
  rig.ws.loop(millis());

  CHECK(!rig.ws.isConnected());
  CHECK_EQ(rig.countOf(WsEvent::Disconnected), 1);
}

TEST(WebSocket, reconnects_after_the_backoff) {
  Rig rig;
  rig.ws.setReconnectInterval(1000);
  rig.open();

  hostsim::dropConnection();
  rig.ws.loop(millis());
  CHECK(!rig.ws.isConnected());

  // Too soon.
  hostsim::advanceMillis(500);
  rig.ws.loop(millis());
  CHECK(!rig.ws.isConnected());

  // Past the backoff: a fresh handshake goes out.
  hostsim::netClearSent();
  hostsim::advanceMillis(800);
  rig.ws.loop(millis());

  CHECK(hostsim::netSent().find("GET /bot") != std::string::npos);
}
