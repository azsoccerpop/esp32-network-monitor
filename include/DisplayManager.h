#pragma once
#include <Arduino.h>

class DisplayManager {
public:
  static void begin();
  static void loop();
  static void setBrightness(uint8_t b);

  // Page navigation -- wired up to the rotary encoder once it exists.
  // Wraps around at either end. Safe to call before the encoder exists too
  // (e.g. from a temporary web UI test button), which is how page-switching
  // gets exercised end-to-end ahead of the physical hardware.
  static void nextPage();
  static void previousPage();
  static const char *currentPageName();

  // pageNumber must be 2-5. Renames the corresponding placeholder page and
  // takes effect immediately on the physical display -- no reboot needed.
  // Called by WebInterface after persisting the new name via
  // HostMonitor::savePageName().
  static void setPageName(uint8_t pageNumber, const String &name);
};
