#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "DisplayManager.h"
#include "HostMonitor.h"
#include "Logger.h"
#include "WebInterface.h"
#include "wifi_manager.h"

// How often to drop an RSSI/uptime checkpoint into the log, useful for
// correlating display corruption with WiFi signal strength when the device
// is deployed somewhere without physical access.
static constexpr uint32_t kStatusLogIntervalMs = 60UL * 1000UL;
static uint32_t s_lastStatusLogMs = 0;

void setup() {
  Serial.begin(115200);
  Logger::begin();
  Logger::logBootInfo();
  Serial.println("Network Monitor starting...");

  if (!LittleFS.begin()) {
    Logger::log("LittleFS mount failed -- host/settings persistence and web assets will be unavailable");
  }

  WifiManager::begin();
  if (WiFi.status() == WL_CONNECTED) {
    Logger::log("WiFi connected, RSSI: " + String(WiFi.RSSI()) + " dBm, IP: " + WiFi.localIP().toString());
  } else {
    Logger::log("WiFi not yet connected -- NETMON_SETUP portal may be active, or still connecting");
  }

  HostMonitor::begin();
  DisplayManager::begin();
  WebInterface::begin();
}

void loop() {
  WifiManager::loop();
  HostMonitor::loop();
  WebInterface::loop();
  DisplayManager::loop();

  const uint32_t now = millis();
  if (now - s_lastStatusLogMs >= kStatusLogIntervalMs) {
    s_lastStatusLogMs = now;
    const int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    Logger::log("Status check: uptime=" + String(now / 1000) + "s RSSI=" + String(rssi) +
                "dBm freeHeap=" + String(ESP.getFreeHeap()));
  }

  delay(100);
}