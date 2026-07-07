#include <Arduino.h>
#include <LittleFS.h>
#include "DisplayManager.h"
#include "HostMonitor.h"
#include "WebInterface.h"
#include "wifi_manager.h"

void setup() {
  Serial.begin(115200);
  Serial.println("Network Monitor starting...");

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed -- host/settings persistence and web assets will be unavailable");
  }

  connectToWiFi();
  HostMonitor::begin();
  DisplayManager::begin();
  WebInterface::begin();
}

void loop() {
  HostMonitor::loop();
  WebInterface::loop();
  DisplayManager::loop();

  delay(100);
}