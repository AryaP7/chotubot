#pragma once
#include <Arduino.h>

// =====================================================
// EXPRESSION MAP
//
// Semantic expression -> real animation name.
//
// Other modules ask for an Expression. They never name
// an animation and never touch OLED frames.
//
// Every string returned below is verified against the
// 18 animations actually present in
// mochi_animations_128x64.h. Nothing here is invented.
// =====================================================

enum class Expression : uint8_t {
  Idle,
  Listening,
  Thinking,
  Speaking,
  Happy,
  Excited,
  Confused,
  Angry,
  Sleepy,
  Love,
  Surprised,
  Notification,
  Error,
  Music,
  Boot,
};

// Returns a name guaranteed to exist in the animation
// table. Where no exact match exists, the closest
// available animation is used - documented per case in
// the .cpp rather than papered over.
const char* animationFor(Expression expression);

const char* expressionName(Expression expression);

// Parse a backend-supplied expression name, e.g. "happy".
// Case-insensitive. Returns false and leaves `out`
// untouched for anything unrecognised - an unknown name
// must never silently become some other mood.
bool expressionFromName(const char* name, Expression& out);
