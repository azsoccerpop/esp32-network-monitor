#include "HostMonitor.h"
#include <Arduino.h>

static std::vector<HostEntry> s_hosts;

void HostMonitor::begin() {
  Serial.println("HostMonitor: begin");
  // Load hosts from LittleFS or data/hosts.json (placeholder)
}

void HostMonitor::loop() {
  // Perform async ping scheduling (placeholder)
}

const std::vector<HostEntry>& HostMonitor::getHosts() {
  return s_hosts;
}
