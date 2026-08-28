#include "ExpressionMap.h"

// Available animations, verified against the header:
//   angry, angry_2, confused_2, content, determined,
//   embarrassed, excited_2, frustrated, happy, happy_2,
//   intro, laugh, love, music, proud, relaxed, sleepy,
//   sleepy_3
//
// There is no "surprised", "listening", "thinking" or
// "error" asset. Those four map to the nearest existing
// animation and are marked APPROX below.

const char* animationFor(Expression expression) {
  switch (expression) {
    case Expression::Idle:         return "content";
    case Expression::Listening:    return "determined";   // APPROX - attentive
    case Expression::Thinking:     return "confused_2";   // APPROX - puzzling
    case Expression::Speaking:     return "happy_2";      // APPROX - animated
    case Expression::Happy:        return "happy";
    case Expression::Excited:      return "excited_2";
    case Expression::Confused:     return "confused_2";
    case Expression::Angry:        return "angry";
    case Expression::Sleepy:       return "sleepy";
    case Expression::Love:         return "love";
    case Expression::Surprised:    return "excited_2";    // APPROX - no asset
    case Expression::Notification: return "excited_2";    // APPROX - attention
    case Expression::Error:        return "frustrated";   // APPROX - no asset
    case Expression::Music:        return "music";
    case Expression::Boot:         return "intro";
  }
  return "happy";
}

bool expressionFromName(const char* name, Expression& out) {
  if (name == nullptr || name[0] == '\0') return false;

  struct Entry {
    const char* name;
    Expression value;
  };

  static const Entry kEntries[] = {
      {"idle", Expression::Idle},
      {"listening", Expression::Listening},
      {"thinking", Expression::Thinking},
      {"speaking", Expression::Speaking},
      {"happy", Expression::Happy},
      {"excited", Expression::Excited},
      {"confused", Expression::Confused},
      {"angry", Expression::Angry},
      {"sleepy", Expression::Sleepy},
      {"love", Expression::Love},
      {"surprised", Expression::Surprised},
      {"notification", Expression::Notification},
      {"error", Expression::Error},
      {"music", Expression::Music},
      {"boot", Expression::Boot},
  };

  for (const Entry& e : kEntries) {
    if (strcasecmp(name, e.name) == 0) {
      out = e.value;
      return true;
    }
  }
  return false;
}

const char* expressionName(Expression expression) {
  switch (expression) {
    case Expression::Idle:         return "IDLE";
    case Expression::Listening:    return "LISTENING";
    case Expression::Thinking:     return "THINKING";
    case Expression::Speaking:     return "SPEAKING";
    case Expression::Happy:        return "HAPPY";
    case Expression::Excited:      return "EXCITED";
    case Expression::Confused:     return "CONFUSED";
    case Expression::Angry:        return "ANGRY";
    case Expression::Sleepy:       return "SLEEPY";
    case Expression::Love:         return "LOVE";
    case Expression::Surprised:    return "SURPRISED";
    case Expression::Notification: return "NOTIFICATION";
    case Expression::Error:        return "ERROR";
    case Expression::Music:        return "MUSIC";
    case Expression::Boot:         return "BOOT";
  }
  return "?";
}
