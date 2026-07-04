#pragma once
#include <Arduino.h>

class DisplayManager {
public:
  static void begin();
  static void loop();
  static void setBrightness(uint8_t b);
};
