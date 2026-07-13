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

struct Settings {
  uint8_t brightness = 128;
  uint16_t ping_interval_sec = DEFAULT_PING_INTERVAL_SEC;
  uint16_t ping_timeout_ms = DEFAULT_PING_TIMEOUT_MS;
  uint16_t max_hosts = MAX_HOSTS;
};

// Outcome of the last attempt to load hosts.json, so callers (e.g. the OLED
// display) can tell an intentionally-empty list apart from a failed load.
enum class HostsLoadStatus {
  Ok,
  FileNotFound,
  ParseError
};

class HostMonitor {
public:
  static void begin();
  static void loop();
  static const std::vector<HostEntry>& getHosts();
  static HostsLoadStatus getHostsLoadStatus();
  static void addHost(const String &name, const String &host);
  static bool removeHost(uint16_t id);
  static Settings getSettings();
  static void saveBrightness(uint8_t b);
private:
  static void loadHosts();
  static void loadSettings();
  static void saveHosts();
  static void saveSettings();
};