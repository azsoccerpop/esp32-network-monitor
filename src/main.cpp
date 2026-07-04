#include <Arduino.h>
#include "DisplayManager.h"
#include "HostMonitor.h"
#include "WebInterface.h"

void setup() {
  Serial.begin(115200);
  Serial.println("Network Monitor starting...");

  DisplayManager::begin();
  HostMonitor::begin();
  WebInterface::begin();
}

void loop() {
  HostMonitor::loop();
  WebInterface::loop();
  DisplayManager::loop();

  delay(100);
}