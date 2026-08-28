#include "MiniWebSocket.h"

#include <esp_random.h>

namespace {

constexpr uint8_t OP_CONTINUATION = 0x0;
constexpr uint8_t OP_TEXT = 0x1;
constexpr uint8_t OP_BINARY = 0x2;
constexpr uint8_t OP_CLOSE = 0x8;
constexpr uint8_t OP_PING = 0x9;
constexpr uint8_t OP_PONG = 0xA;

constexpr uint32_t kConnectTimeoutMs = 4000;
constexpr uint32_t kHandshakeTimeoutMs = 4000;

// Local base64 rather than mbedtls_base64_encode, which
// would pull the very library this class exists to avoid.
const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64Encode(const uint8_t* in, size_t len, char* out) {
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t a = in[i];
    const uint32_t b = (i + 1 < len) ? in[i + 1] : 0;
    const uint32_t c = (i + 2 < len) ? in[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;

    out[o++] = kB64[(triple >> 18) & 0x3F];
    out[o++] = kB64[(triple >> 12) & 0x3F];
    out[o++] = (i + 1 < len) ? kB64[(triple >> 6) & 0x3F] : '=';
    out[o++] = (i + 2 < len) ? kB64[triple & 0x3F] : '=';
  }
  out[o] = '\0';
}

}  // namespace

void MiniWebSocket::begin(const char* host, uint16_t port, const char* path) {
  strncpy(host_, host, sizeof(host_) - 1);
  host_[sizeof(host_) - 1] = '\0';
  strncpy(path_, path, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
  port_ = port;

  enabled_ = true;
  state_ = State::Idle;
  nextAttemptMs_ = 0;
}

void MiniWebSocket::disconnect() {
  if (state_ == State::Open) {
    const uint8_t empty = 0;
    sendFrame(OP_CLOSE, &empty, 0);
  }
  client_.stop();

  const bool wasOpen = (state_ == State::Open);
  state_ = State::Idle;
  enabled_ = false;
  headerMatch_ = 0;

  if (wasOpen && callback_) callback_(WsEvent::Disconnected, nullptr, 0);
}

void MiniWebSocket::fail(const char* reason) {
  Serial.print(F("[ws] "));
  Serial.println(reason);

  const bool wasOpen = (state_ == State::Open);
  client_.stop();
  state_ = State::Idle;
  headerMatch_ = 0;

  if (wasOpen && callback_) callback_(WsEvent::Disconnected, nullptr, 0);
}

bool MiniWebSocket::startConnect(uint32_t nowMs) {
  client_.setTimeout(kConnectTimeoutMs / 1000);

  if (!client_.connect(host_, port_, kConnectTimeoutMs)) {
    nextAttemptMs_ = nowMs + reconnectIntervalMs_;
    return false;
  }

  client_.setNoDelay(true);

  uint8_t nonce[16];
  for (uint8_t i = 0; i < sizeof(nonce); ++i) {
    nonce[i] = static_cast<uint8_t>(esp_random() & 0xFF);
  }

  char key[25];
  base64Encode(nonce, sizeof(nonce), key);

  client_.printf(
      "GET %s HTTP/1.1\r\n"
      "Host: %s:%u\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: %s\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n",
      path_, host_, static_cast<unsigned>(port_), key);

  handshakeStartedMs_ = nowMs;
  headerMatch_ = 0;
  statusLen_ = 0;
  statusLine_[0] = '\0';
  state_ = State::Handshaking;
  return true;
}

bool MiniWebSocket::readHandshakeResponse(uint32_t nowMs) {
  // Scan for the blank line that ends the headers. The
  // status line is checked as it goes past.
  static const char kTerminator[] = "\r\n\r\n";

  while (client_.available() > 0) {
    const int c = client_.read();
    if (c < 0) break;

    // Accumulate just enough of the status line to see
    // the response code.
    if (headerMatch_ == 0 && statusLen_ < sizeof(statusLine_) - 1) {
      if (c != '\r' && c != '\n') {
        statusLine_[statusLen_++] = static_cast<char>(c);
        statusLine_[statusLen_] = '\0';
      }
    }

    if (static_cast<char>(c) == kTerminator[headerMatch_]) {
      headerMatch_++;
      if (headerMatch_ == 4) {
        // Sec-WebSocket-Accept is deliberately not
        // verified: this connects to one known backend on
        // the local network, and the SHA-1 needed to check
        // it is not worth the flash. A wrong server simply
        // will not speak valid frames.
        if (strstr(statusLine_, "101") == nullptr) {
          fail("handshake rejected");
          return false;
        }
        state_ = State::Open;
        Serial.println(F("[ws] open"));
        if (callback_) callback_(WsEvent::Connected, nullptr, 0);
        return true;
      }
    } else {
      headerMatch_ = (static_cast<char>(c) == '\r') ? 1 : 0;
    }
  }

  if (nowMs - handshakeStartedMs_ > kHandshakeTimeoutMs) {
    fail("handshake timeout");
    nextAttemptMs_ = nowMs + reconnectIntervalMs_;
  }
  return false;
}

bool MiniWebSocket::sendFrame(uint8_t opcode, const uint8_t* payload,
                              size_t length) {
  if (!client_.connected()) return false;

  uint8_t header[14];
  size_t h = 0;

  header[h++] = static_cast<uint8_t>(0x80 | opcode);   // FIN + opcode

  // Client frames must always be masked.
  if (length < 126) {
    header[h++] = static_cast<uint8_t>(0x80 | length);
  } else if (length <= 0xFFFF) {
    header[h++] = 0x80 | 126;
    header[h++] = static_cast<uint8_t>((length >> 8) & 0xFF);
    header[h++] = static_cast<uint8_t>(length & 0xFF);
  } else {
    header[h++] = 0x80 | 127;
    for (int i = 7; i >= 0; --i) {
      header[h++] = static_cast<uint8_t>(
          (static_cast<uint64_t>(length) >> (8 * i)) & 0xFF);
    }
  }

  uint8_t mask[4];
  for (uint8_t i = 0; i < 4; ++i) {
    mask[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    header[h++] = mask[i];
  }

  if (client_.write(header, h) != h) return false;

  // Mask in place in small chunks so a large audio frame
  // does not need a second full-size buffer.
  uint8_t chunk[128];
  size_t sent = 0;
  while (sent < length) {
    const size_t n = min(sizeof(chunk), length - sent);
    for (size_t i = 0; i < n; ++i) {
      chunk[i] = payload[sent + i] ^ mask[(sent + i) & 3];
    }
    if (client_.write(chunk, n) != n) return false;
    sent += n;
  }

  return true;
}

void MiniWebSocket::pumpFrames() {
  while (client_.available() >= 2) {
    const uint8_t b0 = static_cast<uint8_t>(client_.read());
    const uint8_t b1 = static_cast<uint8_t>(client_.read());

    const uint8_t opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    uint64_t length = b1 & 0x7F;

    if (length == 126) {
      if (client_.available() < 2) return;
      length = (static_cast<uint64_t>(client_.read()) << 8);
      length |= static_cast<uint64_t>(client_.read());
    } else if (length == 127) {
      if (client_.available() < 8) return;
      length = 0;
      for (int i = 0; i < 8; ++i) {
        length = (length << 8) | static_cast<uint64_t>(client_.read());
      }
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
      if (client_.available() < 4) return;
      for (uint8_t i = 0; i < 4; ++i) {
        mask[i] = static_cast<uint8_t>(client_.read());
      }
    }

    if (opcode == OP_CLOSE) {
      fail("closed by server");
      return;
    }

    // Read the payload in buffer-sized pieces.
    uint64_t remaining = length;
    uint64_t consumed = 0;
    bool dropped = false;

    while (remaining > 0) {
      const size_t want =
          static_cast<size_t>(min<uint64_t>(remaining, kRxBufferSize));

      size_t got = 0;
      const uint32_t deadline = millis() + 200;
      while (got < want && millis() < deadline) {
        const int n = client_.read(rxBuffer_ + got, want - got);
        if (n > 0) got += static_cast<size_t>(n);
      }
      if (got == 0) break;

      if (masked) {
        for (size_t i = 0; i < got; ++i) {
          rxBuffer_[i] ^= mask[(consumed + i) & 3];
        }
      }

      if (opcode == OP_PING) {
        sendFrame(OP_PONG, rxBuffer_, got);
      } else if (opcode == OP_TEXT) {
        // A control message must arrive whole or not at
        // all - half a JSON object is worse than none.
        if (length <= kRxBufferSize) {
          if (callback_) callback_(WsEvent::Text, rxBuffer_, got);
        } else {
          dropped = true;
        }
      } else if (opcode == OP_BINARY || opcode == OP_CONTINUATION) {
        if (callback_) callback_(WsEvent::Binary, rxBuffer_, got);
      }

      consumed += got;
      remaining -= got;
    }

    if (dropped) {
      Serial.println(F("[ws] oversized text frame dropped"));
    }
  }
}

void MiniWebSocket::loop(uint32_t nowMs) {
  if (!enabled_) return;

  switch (state_) {
    case State::Idle:
      if (nowMs >= nextAttemptMs_) {
        startConnect(nowMs);
      }
      break;

    case State::Handshaking:
      if (!client_.connected()) {
        fail("disconnected during handshake");
        nextAttemptMs_ = nowMs + reconnectIntervalMs_;
        break;
      }
      readHandshakeResponse(nowMs);
      break;

    case State::Open:
      if (!client_.connected()) {
        fail("connection lost");
        nextAttemptMs_ = nowMs + reconnectIntervalMs_;
        break;
      }
      pumpFrames();
      break;

    case State::Connecting:
      break;
  }
}

bool MiniWebSocket::sendText(const char* text, size_t length) {
  if (state_ != State::Open) return false;
  return sendFrame(OP_TEXT, reinterpret_cast<const uint8_t*>(text), length);
}

bool MiniWebSocket::sendBinary(const uint8_t* data, size_t length) {
  if (state_ != State::Open) return false;
  return sendFrame(OP_BINARY, data, length);
}
