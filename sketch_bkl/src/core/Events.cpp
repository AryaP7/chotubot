#include "Events.h"

const char* eventName(EventType type) {
  switch (type) {
    case EventType::TouchShort:           return "TOUCH_SHORT";
    case EventType::TouchDouble:          return "TOUCH_DOUBLE";
    case EventType::TouchLong:            return "TOUCH_LONG";
    case EventType::TouchTriple:          return "TOUCH_TRIPLE";
    case EventType::PersonApproaching:    return "PERSON_APPROACHING";
    case EventType::PersonDetected:       return "PERSON_DETECTED";
    case EventType::PersonLeft:           return "PERSON_LEFT";
    case EventType::GestureApproach:      return "GESTURE_APPROACH";
    case EventType::GestureRetreat:       return "GESTURE_RETREAT";
    case EventType::GestureHover:         return "GESTURE_HOVER";
    case EventType::AlarmFired:           return "ALARM_FIRED";
    case EventType::QuietHoursStarted:    return "QUIET_START";
    case EventType::QuietHoursEnded:      return "QUIET_END";
    case EventType::TimeOfDayChanged:     return "TIME_OF_DAY";
    case EventType::BatteryLow:           return "BATTERY_LOW";
    case EventType::VoiceStarted:         return "VOICE_STARTED";
    case EventType::VoiceStopped:         return "VOICE_STOPPED";
    case EventType::AiRequest:            return "AI_REQUEST";
    case EventType::AiResponse:           return "AI_RESPONSE";
    case EventType::AiError:              return "AI_ERROR";
    case EventType::WakeWordConfirmed:    return "WAKE_CONFIRMED";
    case EventType::WakeWordRejected:     return "WAKE_REJECTED";
    case EventType::SetExpression:        return "SET_EXPRESSION";
    case EventType::BackendPong:          return "BACKEND_PONG";
    case EventType::AudioStarted:         return "AUDIO_STARTED";
    case EventType::AudioFinished:        return "AUDIO_FINISHED";
    case EventType::NotificationReceived: return "NOTIFICATION";
    case EventType::AlarmTriggered:       return "ALARM";
    case EventType::WifiConnected:        return "WIFI_CONNECTED";
    case EventType::WifiDisconnected:     return "WIFI_DISCONNECTED";
    case EventType::RtcError:             return "RTC_ERROR";
    case EventType::SensorError:          return "SENSOR_ERROR";
    case EventType::SpotifyState:         return "SPOTIFY_STATE";
    case EventType::None:                 return "NONE";
  }
  return "UNKNOWN";
}
