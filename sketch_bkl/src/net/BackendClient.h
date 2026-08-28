#pragma once
#include <Arduino.h>

class EventBus;
class AudioOutput;

// =====================================================
// BACKEND CLIENT
//
// WebSocket link to the PC/backend. Chosen over HTTP
// because this connection is bidirectional and long
// lived: audio goes up while the wake-word verdict, bot
// state and TTS audio come down. Polling would add
// latency to every single interaction.
//
// PROTOCOL
//
// ESP32 -> backend (text)
//   {"type":"hello","token":"...","fw":"..."}
//   {"type":"event","event":"TOUCH_SHORT"}
//   {"type":"voice_start"}
//   {"type":"voice_end"}
// ESP32 -> backend (binary)
//   raw int16 mono PCM at 16 kHz, only while voice is
//   active - this is what the VAD gate is protecting.
//
// backend -> ESP32 (text)
//   {"type":"wake","detected":true|false}
//   {"type":"state","state":"THINKING"}
//   {"type":"notification","title":"..."}
//   {"type":"spotify_state","playing":true,"track":"...",
//    "artist":"...","progress_ms":0,"duration_ms":0}
// backend -> ESP32 (binary)
//   int16 mono PCM at 16 kHz, played through the amp.
//
// SECURITY
// The token is checked by the backend on connect. The
// ESP32 holds no third-party credentials of any kind -
// no Spotify tokens, no API keys. See Secrets.example.h.
//
// STATUS: IMPLEMENTED, never tested against a real
// backend - none exists yet.
// =====================================================

enum class SpotifyStatus : uint8_t {
  Idle = 0,
  Loading,
  Playing,
  Paused,
  Error,
};

const char* spotifyStatusName(SpotifyStatus status);

struct Notification {
  char title[28] = {0};
  char body[40] = {0};
  bool valid = false;
};

struct SpotifyState {
  SpotifyStatus status = SpotifyStatus::Idle;
  bool playing = false;
  char track[40] = {0};
  char artist[32] = {0};
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  bool valid = false;
};

class BackendClient {
 public:
  // audio receives any PCM the backend sends down.
  void begin(AudioOutput& audio);

  // Non-blocking. Must be called every loop once Wi-Fi
  // is up; harmless before that.
  void update(uint32_t nowMs, EventBus& bus);

  // Called when Wi-Fi comes and goes.
  void onNetworkUp();
  void onNetworkDown();

  bool isConnected() const;

  void sendEvent(const char* eventName);

  // Liveness probe. The backend answers with "pong",
  // which arrives as EventType::BackendPong.
  void sendPing();

  // Voice uplink. sendAudio() silently does nothing
  // unless a segment is open, so a VAD bug cannot flood
  // the link.
  void beginVoiceSegment();
  void sendAudio(const int16_t* samples, uint16_t count);
  void endVoiceSegment();

  const SpotifyState& spotify() const { return spotify_; }
  const Notification& notification() const { return notification_; }

  // Internal: called from the WebSocket callback, which
  // is a free function and cannot reach private members.
  void applySpotifyState(const SpotifyState& state) { spotify_ = state; }
  void applyNotification(const Notification& n) { notification_ = n; }

 private:
  // Matches HEARTBEAT_INTERVAL_S on the backend. Its
  // timeout is 50 s, so two missed pings are survivable.
  static constexpr uint32_t kHeartbeatIntervalMs = 20000;

  uint32_t lastPingMs_ = 0;
  bool voiceSegmentOpen_ = false;
  SpotifyState spotify_;
  Notification notification_;
};
