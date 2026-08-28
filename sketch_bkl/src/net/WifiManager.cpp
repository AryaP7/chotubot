#include "WifiManager.h"

#include <WiFi.h>

#include "../core/Events.h"
#include "Secrets.h"

namespace {

// How long one association attempt is allowed before we
// give up and back off.
constexpr uint32_t kAttemptTimeoutMs = 12000;

// Backoff between attempts: 2 s, doubling to 60 s. A bot
// on a desk with the router off should not hammer the
// radio all day.
constexpr uint32_t kBackoffBaseMs = 2000;
constexpr uint32_t kBackoffMaxMs = 60000;

uint32_t backoffFor(uint8_t failures) {
  uint32_t delayMs = kBackoffBaseMs;
  for (uint8_t i = 0; i < failures && delayMs < kBackoffMaxMs; ++i) {
    delayMs *= 2;
  }
  return (delayMs > kBackoffMaxMs) ? kBackoffMaxMs : delayMs;
}

}  // namespace

const char* wifiStateName(WifiState state) {
  switch (state) {
    case WifiState::Disabled:   return "DISABLED";
    case WifiState::Idle:       return "IDLE";
    case WifiState::Connecting: return "CONNECTING";
    case WifiState::Connected:  return "CONNECTED";
    case WifiState::Failed:     return "FAILED";
  }
  return "?";
}

void WifiManager::begin() {
  const char* ssid = WIFI_SSID;

  // An unedited Secrets.h should not send the radio into
  // a permanent retry loop.
  if (ssid[0] == '\0' || strcmp(ssid, "your-network") == 0) {
    state_ = WifiState::Disabled;
    Serial.println(F("[wifi] no credentials - staying offline"));
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);   // we drive reconnection ourselves
  state_ = WifiState::Idle;
}

void WifiManager::startAttempt(uint32_t nowMs) {
  Serial.print(F("[wifi] connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  attemptStartedMs_ = nowMs;
  state_ = WifiState::Connecting;
}

void WifiManager::update(uint32_t nowMs, EventBus& bus) {
  if (state_ == WifiState::Disabled) return;

  switch (state_) {
    case WifiState::Idle:
    case WifiState::Failed:
      if (nowMs >= nextAttemptMs_) {
        startAttempt(nowMs);
      }
      break;

    case WifiState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        state_ = WifiState::Connected;
        failureCount_ = 0;

        strncpy(ip_, WiFi.localIP().toString().c_str(), sizeof(ip_) - 1);
        ip_[sizeof(ip_) - 1] = '\0';

        Serial.print(F("[wifi] connected - "));
        Serial.println(ip_);
        bus.publish(EventType::WifiConnected, 0);

      } else if (nowMs - attemptStartedMs_ > kAttemptTimeoutMs) {
        if (failureCount_ < 255) failureCount_++;
        state_ = WifiState::Failed;
        nextAttemptMs_ = nowMs + backoffFor(failureCount_);

        Serial.print(F("[wifi] attempt failed, retry in "));
        Serial.print(backoffFor(failureCount_) / 1000);
        Serial.println(F("s"));
      }
      break;

    case WifiState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        state_ = WifiState::Failed;
        ip_[0] = '\0';
        nextAttemptMs_ = nowMs + kBackoffBaseMs;

        Serial.println(F("[wifi] connection lost"));
        bus.publish(EventType::WifiDisconnected, 0);
      }
      break;

    case WifiState::Disabled:
      break;
  }
}
