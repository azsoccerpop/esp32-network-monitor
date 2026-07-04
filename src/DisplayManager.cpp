#include "DisplayManager.h"
#include <Arduino.h>

#include <LittleFS.h>
#include <SH1106.h>
#include "HostMonitor.h"

static SH1106Wire display(0x3C, 21, 22);
static uint8_t g_brightness = 128;

void DisplayManager::begin() {
  Serial.println("DisplayManager: begin");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed in DisplayManager");
  }
  display.init();
  auto s = HostMonitor::getSettings();
  g_brightness = s.brightness;
  display.setContrast(g_brightness);
  display.clear();
  display.drawString(0, 0, "Network Monitor");
  display.display();
}

void DisplayManager::loop() {
  // Show a simple summary line from host list
  const auto &hosts = HostMonitor::getHosts();
  display.clear();
  display.drawString(0, 0, "Network Monitor");
  if (!hosts.empty()) {
    auto &h = hosts[0];
    String line = h.name + ": " + (h.reachable ? "UP" : "DOWN");
    display.drawString(0, 14, line.c_str());
    String lat = "lat:" + String(h.lastLatencyMs) + "ms";
    display.drawString(0, 30, lat.c_str());
  }
  display.display();
}

void DisplayManager::setBrightness(uint8_t b) {
  g_brightness = b;
  display.setContrast(g_brightness);
  HostMonitor::saveBrightness(g_brightness);
}
