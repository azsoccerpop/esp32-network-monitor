#pragma once
#include <Arduino.h>

// Small in-memory ring buffer of recent log lines, safe to write from the
// main loop task and read from the AsyncWebServer task (guarded by a
// FreeRTOS mutex under the hood). Lines are timestamped with millis() since
// boot. Intended for remote diagnostics (viewable via the web UI) when the
// device is deployed somewhere without physical/serial access -- NOT a
// replacement for Serial output, which still happens alongside this.
class Logger {
public:
  static void begin();

  // Records a line both to Serial and to the in-memory ring buffer.
  static void log(const String &line);

  // Convenience: logs reset reason, free heap, and uptime. Call once near
  // the top of setup().
  static void logBootInfo();

  // Returns the buffered lines, oldest first, serialized as a JSON array of
  // strings, e.g. ["[12.345] message one","[12.401] message two"].
  static String getLogsJson();

  static void clear();
};
