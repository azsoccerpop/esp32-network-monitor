#pragma once
#include <Arduino.h>

// The current value of one configured widget, as last fetched from
// InfluxDB. `numericValue` is used for barchart scaling; `displayValue` is
// pre-formatted for the table view (units humanized where the field name
// hints at it, e.g. "used_bytes" -> "4.2 TB").
struct MetricValue {
  bool ok = false;
  double numericValue = 0;
  String displayValue;
  String error;
  uint32_t lastUpdatedMs = 0;
};

// Polls all configured widgets (across all 4 pages) from InfluxDB on a
// background FreeRTOS task, independent of the main loop() that
// DisplayManager's I2C timing depends on -- a slow/unreachable Influx
// server degrades to "stale cached values on screen", never a blocked
// display.
class MetricsManager {
public:
  static void begin();

  // pageNumber 2-5, slot 0-3. Returns the last successfully cached value
  // (or ok=false if never successfully fetched).
  static MetricValue getValue(uint8_t pageNumber, uint8_t slot);

  // Re-reads page_config.json-backed widget definitions and polls
  // everything immediately, off the calling task. Call after saving new
  // widget config from the web UI so the display doesn't wait for the next
  // scheduled poll.
  static void refreshNow();

private:
  static void taskFn(void *param);
  static void pollAll();
};
