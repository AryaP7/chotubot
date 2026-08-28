#pragma once
#include <Arduino.h>
#include <NetworkClient.h>

// =====================================================
// MINIMAL WEBSOCKET CLIENT - plain ws:// only
//
// WHY THIS EXISTS
// The Links2004 WebSockets library includes
// <WiFiClientSecure.h> unconditionally for NETWORK_ESP32
// (WebSockets.h line 237). There is no build flag to
// turn it off, so linking it drags in mbedTLS whether or
// not a single byte is ever encrypted.
//
// This bot talks to a backend on the same LAN over
// ws://. TLS is dead weight here, so this implements
// just the client half of RFC 6455 on a plain
// NetworkClient: handshake, masked client frames,
// unmasked server frames, ping/pong, close.
//
// DELIBERATELY NOT IMPLEMENTED
//   wss:// / TLS          - the entire point
//   permessage-deflate    - CPU we do not have to spare
//   fragmented send       - we never send > 64 KB
//   Sec-WebSocket-Accept verification - see connect()
//
// A text frame larger than kRxBufferSize is dropped
// rather than half-parsed. Binary frames larger than the
// buffer are delivered in consecutive chunks, which is
// correct for a PCM audio stream.
// =====================================================

enum class WsEvent : uint8_t {
  Connected,
  Disconnected,
  Text,
  Binary,
};

typedef void (*WsEventCallback)(WsEvent event, const uint8_t* payload,
                                size_t length);

class MiniWebSocket {
 public:
  static constexpr size_t kRxBufferSize = 1024;

  void begin(const char* host, uint16_t port, const char* path);
  void onEvent(WsEventCallback callback) { callback_ = callback; }

  // Non-blocking. Drives connection, reconnection and
  // frame reception.
  void loop(uint32_t nowMs);

  void disconnect();
  bool isConnected() const { return state_ == State::Open; }

  bool sendText(const char* text, size_t length);
  bool sendText(const char* text) { return sendText(text, strlen(text)); }
  bool sendBinary(const uint8_t* data, size_t length);

  void setReconnectInterval(uint32_t ms) { reconnectIntervalMs_ = ms; }

 private:
  enum class State : uint8_t { Idle, Connecting, Handshaking, Open };

  bool startConnect(uint32_t nowMs);
  bool readHandshakeResponse(uint32_t nowMs);
  void pumpFrames();
  bool sendFrame(uint8_t opcode, const uint8_t* payload, size_t length);
  void fail(const char* reason);

  NetworkClient client_;
  WsEventCallback callback_ = nullptr;

  State state_ = State::Idle;
  bool enabled_ = false;

  char host_[40] = {0};
  char path_[40] = {0};
  uint16_t port_ = 0;

  uint32_t nextAttemptMs_ = 0;
  uint32_t handshakeStartedMs_ = 0;
  uint32_t reconnectIntervalMs_ = 5000;

  uint8_t rxBuffer_[kRxBufferSize];
  size_t headerMatch_ = 0;

  // Enough of the HTTP status line to find "101".
  char statusLine_[48] = {0};
  size_t statusLen_ = 0;
};
