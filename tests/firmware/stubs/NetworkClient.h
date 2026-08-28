// Host stub of the ESP32 core's TCP client.
//
// An in-memory loopback: what the firmware writes lands in
// a buffer a test can inspect, and what a test queues is
// what the firmware reads. This lets the hand-written
// RFC 6455 codec in MiniWebSocket run for real, which is
// the riskiest code in the firmware.

#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace hostsim {

extern std::string g_netRxBuffer;   // server -> firmware
extern std::string g_netTxBuffer;   // firmware -> server
extern size_t g_netRxCursor;
extern bool g_netConnectOk;
extern bool g_netConnected;

inline void netQueue(const std::string& data) { g_netRxBuffer += data; }
inline const std::string& netSent() { return g_netTxBuffer; }
inline void netClearSent() { g_netTxBuffer.clear(); }
inline void setNetConnectOk(bool ok) { g_netConnectOk = ok; }
inline void dropConnection() { g_netConnected = false; }

inline void resetNet() {
  g_netRxBuffer.clear();
  g_netTxBuffer.clear();
  g_netRxCursor = 0;
  g_netConnectOk = true;
  g_netConnected = false;
}

}  // namespace hostsim

class NetworkClient {
 public:
  void setTimeout(uint32_t) {}
  void setNoDelay(bool) {}

  bool connect(const char*, uint16_t, uint32_t = 0) {
    hostsim::g_netConnected = hostsim::g_netConnectOk;
    return hostsim::g_netConnected;
  }

  bool connected() { return hostsim::g_netConnected; }

  void stop() { hostsim::g_netConnected = false; }

  int available() {
    return static_cast<int>(hostsim::g_netRxBuffer.size() -
                            hostsim::g_netRxCursor);
  }

  int read() {
    if (hostsim::g_netRxCursor >= hostsim::g_netRxBuffer.size()) return -1;
    return static_cast<uint8_t>(hostsim::g_netRxBuffer[hostsim::g_netRxCursor++]);
  }

  int read(uint8_t* buffer, size_t size) {
    const size_t have = hostsim::g_netRxBuffer.size() - hostsim::g_netRxCursor;
    const size_t take = (size < have) ? size : have;
    if (take == 0) return 0;

    memcpy(buffer, hostsim::g_netRxBuffer.data() + hostsim::g_netRxCursor, take);
    hostsim::g_netRxCursor += take;
    return static_cast<int>(take);
  }

  size_t write(const uint8_t* data, size_t size) {
    hostsim::g_netTxBuffer.append(reinterpret_cast<const char*>(data), size);
    return size;
  }

  size_t printf(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (n > 0) hostsim::g_netTxBuffer.append(buffer, static_cast<size_t>(n));
    return (n > 0) ? static_cast<size_t>(n) : 0;
  }
};
