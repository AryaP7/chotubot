#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// =====================================================
// DISPLAY MANAGER
//
// Sole owner of the SH1106 U8g2 instance. Screens draw
// through gfx(); nothing else constructs a display.
// =====================================================

class DisplayManager {
 public:
  bool begin();

  U8G2& gfx() { return oled_; }

  // Direct access to the full-screen buffer, for blits
  // that write every byte and so need neither clearing
  // nor clipping.
  uint8_t* buffer() { return oled_.getBufferPtr(); }

  // Centred single-line message. Used for boot and fault
  // reporting before any richer screen exists.
  void showMessage(const char* line);

  bool isReady() const { return ready_; }

 private:
  U8G2_SH1106_128X64_NONAME_F_HW_I2C oled_{U8G2_R0, U8X8_PIN_NONE};
  bool ready_ = false;
};
