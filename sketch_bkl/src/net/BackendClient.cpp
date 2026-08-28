#include "BackendClient.h"

#include <ArduinoJson.h>

#include "../audio/AudioOutput.h"
#include "../core/Events.h"
#include "../display/ExpressionMap.h"
#include "MiniWebSocket.h"
#include "Secrets.h"

// MiniWebSocket takes a plain function pointer, so the
// active instance and its collaborators are reached
// through file statics. There is only ever one link.
static MiniWebSocket ws;
static BackendClient* self = nullptr;
static EventBus* busRef = nullptr;
static AudioOutput* audioRef = nullptr;
static bool linkUp = false;

const char* spotifyStatusName(SpotifyStatus status) {
  switch (status) {
    case SpotifyStatus::Idle:    return "IDLE";
    case SpotifyStatus::Loading: return "LOADING";
    case SpotifyStatus::Playing: return "PLAYING";
    case SpotifyStatus::Paused:  return "PAUSED";
    case SpotifyStatus::Error:   return "ERROR";
  }
  return "?";
}

namespace {

// Bumped only for a breaking protocol change. Adding a
// message type is not breaking - both sides ignore what
// they do not recognise.
constexpr int kProtocolVersion = 1;

void sendHello() {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["token"] = BACKEND_TOKEN;
  doc["fw"] = "chotubot-phaseB";
  doc["v"] = kProtocolVersion;

  char out[160];
  const size_t n = serializeJson(doc, out, sizeof(out));
  ws.sendText(out, n);
}

void handleText(const uint8_t* payload, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.println(F("[backend] malformed message ignored"));
    return;
  }

  const char* type = doc["type"] | "";

  if (strcmp(type, "pong") == 0) {
    busRef->publish(EventType::BackendPong);
    return;
  }

  if (strcmp(type, "welcome") == 0) {
    // The backend tells us which version it speaks and how
    // often it wants to hear from us.
    Serial.print(F("[backend] welcome v"));
    Serial.print(doc["v"] | 0);
    Serial.print(F(" heartbeat "));
    Serial.print(doc["heartbeat_s"] | 0);
    Serial.println('s');
    return;
  }

  if (strcmp(type, "error") == 0) {
    // Reported, never acted on blindly. A backend error is
    // information, not an instruction.
    Serial.print(F("[backend] error ["));
    Serial.print(doc["code"] | "?");
    Serial.print(F("] "));
    Serial.println(doc["message"] | "");
    return;
  }

  if (strcmp(type, "expression") == 0) {
    // Validated against the known set. An unrecognised
    // name is logged and ignored, never guessed at.
    Expression expression;
    const char* value = doc["value"] | "";

    if (expressionFromName(value, expression)) {
      busRef->publish(EventType::SetExpression,
                      static_cast<int32_t>(expression));
    } else {
      Serial.print(F("[backend] unknown expression: "));
      Serial.println(value);
    }
    return;
  }

  if (strcmp(type, "wake") == 0) {
    // The wake word is decided here, on the backend.
    // See DECISIONS.md D1.
    const bool detected = doc["detected"] | false;
    busRef->publish(detected ? EventType::WakeWordConfirmed
                             : EventType::WakeWordRejected);
    return;
  }

  if (strcmp(type, "state") == 0) {
    const char* state = doc["state"] | "";
    if (strcmp(state, "THINKING") == 0) {
      busRef->publish(EventType::AiRequest);
    } else if (strcmp(state, "ERROR") == 0) {
      busRef->publish(EventType::AiError);
    } else if (strcmp(state, "SPEAKING") == 0) {
      busRef->publish(EventType::AudioStarted);
    }
    return;
  }

  if (strcmp(type, "notification") == 0) {
    if (self != nullptr) {
      Notification n;
      strncpy(n.title, doc["title"] | "Notification", sizeof(n.title) - 1);
      strncpy(n.body, doc["body"] | "", sizeof(n.body) - 1);
      n.valid = true;
      self->applyNotification(n);
    }
    busRef->publish(EventType::NotificationReceived);
    return;
  }

  if (strcmp(type, "spotify_state") == 0 && self != nullptr) {
    // Stored, not acted on. Playback lives on a real
    // Spotify device driven by the backend - the bot is
    // a controller and a display, not a speaker for it.
    SpotifyState s;
    s.playing = doc["playing"] | false;
    strncpy(s.track, doc["track"] | "", sizeof(s.track) - 1);
    strncpy(s.artist, doc["artist"] | "", sizeof(s.artist) - 1);
    s.progressMs = doc["progress_ms"] | 0;
    s.durationMs = doc["duration_ms"] | 0;
    s.valid = true;

    // The backend may name the status directly; if not,
    // fall back to the playing flag.
    const char* status = doc["status"] | "";
    if (strcmp(status, "loading") == 0)      s.status = SpotifyStatus::Loading;
    else if (strcmp(status, "error") == 0)   s.status = SpotifyStatus::Error;
    else if (s.playing)                      s.status = SpotifyStatus::Playing;
    else if (s.track[0] != '\0')             s.status = SpotifyStatus::Paused;
    else                                     s.status = SpotifyStatus::Idle;

    self->applySpotifyState(s);
    busRef->publish(EventType::SpotifyState, s.playing ? 1 : 0);
    return;
  }
}

void onWsEvent(WsEvent event, const uint8_t* payload, size_t length) {
  switch (event) {
    case WsEvent::Connected:
      linkUp = true;
      Serial.println(F("[backend] connected"));
      sendHello();
      break;

    case WsEvent::Disconnected:
      linkUp = false;
      Serial.println(F("[backend] disconnected"));
      break;

    case WsEvent::Text:
      if (busRef) handleText(payload, length);
      break;

    case WsEvent::Binary:
      // TTS audio. Straight to the amplifier.
      if (audioRef && length >= sizeof(int16_t)) {
        audioRef->writePcm(reinterpret_cast<const int16_t*>(payload),
                           length / sizeof(int16_t));
      }
      break;
  }
}

}  // namespace

void BackendClient::begin(AudioOutput& audio) {
  self = this;
  audioRef = &audio;
  ws.onEvent(onWsEvent);

  // The library reconnects on its own; we only gate when
  // it is allowed to try.
  ws.setReconnectInterval(5000);
}

void BackendClient::onNetworkUp() {
  Serial.print(F("[backend] linking to "));
  Serial.print(BACKEND_HOST);
  Serial.print(':');
  Serial.println(BACKEND_PORT);

  ws.begin(BACKEND_HOST, BACKEND_PORT, BACKEND_PATH);
}

void BackendClient::onNetworkDown() {
  voiceSegmentOpen_ = false;
  linkUp = false;
  ws.disconnect();
}

bool BackendClient::isConnected() const {
  return linkUp;
}

void BackendClient::update(uint32_t nowMs, EventBus& bus) {
  busRef = &bus;
  ws.loop(nowMs);

  // Heartbeat. The backend drops a session that goes quiet
  // for longer than its timeout, so this both proves we are
  // alive and gives us a round trip to notice a link that
  // has died without the socket closing.
  if (linkUp && (nowMs - lastPingMs_) >= kHeartbeatIntervalMs) {
    lastPingMs_ = nowMs;
    sendPing();
  }
}

void BackendClient::sendEvent(const char* eventName) {
  if (!linkUp) return;

  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = eventName;

  char out[96];
  const size_t n = serializeJson(doc, out, sizeof(out));
  ws.sendText(out, n);
}

void BackendClient::sendPing() {
  if (!linkUp) return;
  ws.sendText("{\"type\":\"ping\"}");
}

void BackendClient::beginVoiceSegment() {
  if (!linkUp || voiceSegmentOpen_) return;
  voiceSegmentOpen_ = true;
  ws.sendText("{\"type\":\"voice_start\"}");
}

void BackendClient::sendAudio(const int16_t* samples, uint16_t count) {
  // The gate: no open segment, nothing goes out. This is
  // what stops the bot streaming silence all day.
  if (!linkUp || !voiceSegmentOpen_ || samples == nullptr || count == 0) return;

  ws.sendBinary(reinterpret_cast<const uint8_t*>(samples),
                static_cast<size_t>(count) * sizeof(int16_t));
}

void BackendClient::endVoiceSegment() {
  if (!linkUp || !voiceSegmentOpen_) return;
  voiceSegmentOpen_ = false;
  ws.sendText("{\"type\":\"voice_end\"}");
}
