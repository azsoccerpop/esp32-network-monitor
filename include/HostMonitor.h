#pragma once
#include <Arduino.h>
#include <vector>

struct HostEntry {
  uint16_t id;
  String name;
  String host;
  bool enabled;
  bool reachable;
  uint32_t lastLatencyMs;
};

class HostMonitor {
public:
  static void begin();
  static void loop();
  static const std::vector<HostEntry>& getHosts();
};
