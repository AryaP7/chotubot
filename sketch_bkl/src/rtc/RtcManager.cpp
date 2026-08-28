#include "RtcManager.h"

bool RtcManager::begin() {
  ready_ = rtc_.begin();

  if (!ready_) {
    Serial.println(F("[rtc] DS3231 not found"));
    return false;
  }

  lostPower_ = rtc_.lostPower();
  if (lostPower_) {
    Serial.println(F("[rtc] DS3231 lost power - time is not trustworthy"));
  }

  return true;
}

DateTime RtcManager::now() {
  if (!ready_) {
    return DateTime(static_cast<uint32_t>(0));
  }
  return rtc_.now();
}

void RtcManager::setDateTime(const DateTime& dt) {
  if (!ready_) return;
  rtc_.adjust(dt);
  lostPower_ = false;
  Serial.println(F("[rtc] time set"));
}
