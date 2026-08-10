#pragma once
#include <Arduino.h>

// Result of a one-off connection test against an InfluxDB 1.x server,
// triggered by the "Save & Test" button in the web UI. Runs synchronously
// (blocking HTTP calls) -- acceptable here because it's invoked from an
// AsyncWebServer request handler, which runs on its own task, not the main
// loop() that DisplayManager's I2C timing depends on.
struct InfluxTestResult {
  bool reachable = false;    // /ping succeeded -- host/port are correct and something is listening
  bool authOk = false;       // SHOW DATABASES succeeded -- credentials (if any) are correct
  bool databaseFound = false;  // the named database is in the server's database list
  String influxVersion;      // from the X-Influxdb-Version response header, if present
  String message;            // human-readable summary shown in the web UI
};

// database/username/password may be empty strings. Blocks for up to a few
// seconds (network timeouts) -- call only from a context where that's fine,
// not from the main loop().
InfluxTestResult testInfluxConnection(const String &host, uint16_t port, const String &database,
                                       const String &username, const String &password);
