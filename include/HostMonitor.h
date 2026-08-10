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

  // User-configurable OLED page names for pages 2-5 are stored separately
  // in PageConfigStore (page_config.json), alongside their format and
  // widget config -- not here.

  // InfluxDB connection settings. "version" is reserved for future 2.x
  // support (different auth model: token + org/bucket instead of
  // username/password + database, and Flux instead of InfluxQL) -- only
  // 1.x is actually implemented right now, but storing this now means
  // adding 2.x later doesn't require a settings-schema migration.
  String influx_version = "1.x";
  String influx_host = "";
  uint16_t influx_port = 8086;
  String influx_database = "";
  String influx_username = "";
  String influx_password = "";
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

  // password: pass an empty string to leave the currently-stored password
  // unchanged (lets the web UI submit the form without forcing the user to
  // re-enter a password that's already saved).
  static void saveInfluxConfig(const String &host, uint16_t port, const String &database,
                                const String &username, const String &password);
private:
  static void loadHosts();
  static void loadSettings();
  static void saveHosts();
  static void saveSettings();
};
