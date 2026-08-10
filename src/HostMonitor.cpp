#include "HostMonitor.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP32Ping.h>
#include <algorithm>

static std::vector<HostEntry> s_hosts;
static Settings s_settings;
static uint32_t s_lastPingMs = 0;
// Monotonically increasing counter for host IDs. Recomputed on load from the
// max ID actually present in hosts.json (see loadHosts()), then only ever
// incremented -- never reused -- so IDs stay stable across reboots and can't
// collide with a stale ID the web UI might still be holding.
static uint16_t s_nextId = 1;
static HostsLoadStatus s_hostsLoadStatus = HostsLoadStatus::Ok;

void HostMonitor::loadHosts() {
  s_hosts.clear();
  s_nextId = 1;

  const char* path = LittleFS.exists("/hosts.json") ? "/hosts.json" : "/data/hosts.json";
  if (!LittleFS.exists(path)) {
    Serial.println("hosts.json not found, using defaults");
    s_hostsLoadStatus = HostsLoadStatus::FileNotFound;
    return;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    s_hostsLoadStatus = HostsLoadStatus::FileNotFound;
    return;
  }
  size_t size = f.size();
  std::unique_ptr<char[]> buf(new char[size+1]);
  f.readBytes(buf.get(), size);
  buf[size] = '\0';
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, buf.get());
  if (err) {
    Serial.println("Failed to parse hosts.json");
    s_hostsLoadStatus = HostsLoadStatus::ParseError;
    return;
  }
  s_hostsLoadStatus = HostsLoadStatus::Ok;
  for (JsonObject obj : doc.as<JsonArray>()) {
    HostEntry h;
    // Preserve the persisted id so ids stay stable across reboots. Fall back
    // to the next available id only if this entry doesn't have one (e.g. a
    // hand-edited or older hosts.json).
    h.id = obj["id"] | 0;
    if (h.id == 0) {
      h.id = s_nextId;
    }
    if (h.id >= s_nextId) {
      s_nextId = h.id + 1;
    }
    const char* name = obj["name"] | "";
    const char* host = obj["host"] | "";
    h.name = String(name);
    h.host = String(host);
    h.enabled = obj["enabled"] | true;
    h.reachable = false;
    h.lastLatencyMs = 0;
    s_hosts.push_back(h);
  }
}

void HostMonitor::saveHosts() {
  File f = LittleFS.open("/hosts.json", "w");
  if (!f) {
    f = LittleFS.open("/data/hosts.json", "w");
  }
  if (!f) return;
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &h : s_hosts) {
    JsonObject o = arr.createNestedObject();
    o["id"] = h.id;
    o["name"] = h.name.c_str();
    o["host"] = h.host.c_str();
    o["enabled"] = h.enabled;
  }
  serializeJson(doc, f);
  f.close();
}

void HostMonitor::loadSettings() {
  const char* path = LittleFS.exists("/settings.json") ? "/settings.json" : "/data/settings.json";
  if (!LittleFS.exists(path)) return;

  File f = LittleFS.open(path, "r");
  if (!f) return;
  size_t size = f.size();
  std::unique_ptr<char[]> buf(new char[size+1]);
  f.readBytes(buf.get(), size);
  buf[size] = '\0';
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, buf.get());
  if (err) return;
  s_settings.brightness = doc["brightness"] | s_settings.brightness;
  s_settings.ping_interval_sec = doc["ping_interval_sec"] | s_settings.ping_interval_sec;
  s_settings.ping_timeout_ms = doc["ping_timeout_ms"] | s_settings.ping_timeout_ms;
  s_settings.max_hosts = doc["max_hosts"] | s_settings.max_hosts;

  s_settings.page2_name = doc["page2_name"] | s_settings.page2_name;
  s_settings.page3_name = doc["page3_name"] | s_settings.page3_name;
  s_settings.page4_name = doc["page4_name"] | s_settings.page4_name;
  s_settings.page5_name = doc["page5_name"] | s_settings.page5_name;

  s_settings.influx_version = doc["influx_version"] | s_settings.influx_version;
  s_settings.influx_host = doc["influx_host"] | s_settings.influx_host;
  s_settings.influx_port = doc["influx_port"] | s_settings.influx_port;
  s_settings.influx_database = doc["influx_database"] | s_settings.influx_database;
  s_settings.influx_username = doc["influx_username"] | s_settings.influx_username;
  s_settings.influx_password = doc["influx_password"] | s_settings.influx_password;
}

void HostMonitor::saveSettings() {
  File f = LittleFS.open("/settings.json", "w");
  if (!f) {
    f = LittleFS.open("/data/settings.json", "w");
  }
  if (!f) return;
  DynamicJsonDocument doc(2048);
  doc["brightness"] = s_settings.brightness;
  doc["ping_interval_sec"] = s_settings.ping_interval_sec;
  doc["ping_timeout_ms"] = s_settings.ping_timeout_ms;
  doc["max_hosts"] = s_settings.max_hosts;

  doc["page2_name"] = s_settings.page2_name;
  doc["page3_name"] = s_settings.page3_name;
  doc["page4_name"] = s_settings.page4_name;
  doc["page5_name"] = s_settings.page5_name;

  doc["influx_version"] = s_settings.influx_version;
  doc["influx_host"] = s_settings.influx_host;
  doc["influx_port"] = s_settings.influx_port;
  doc["influx_database"] = s_settings.influx_database;
  doc["influx_username"] = s_settings.influx_username;
  doc["influx_password"] = s_settings.influx_password;
  serializeJson(doc, f);
  f.close();
}

void HostMonitor::begin() {
  Serial.println("HostMonitor: begin");
  s_settings = Settings();
  loadSettings();
  loadHosts();
  s_lastPingMs = millis();
}

void HostMonitor::addHost(const String &name, const String &host) {
  if (s_hosts.size() >= s_settings.max_hosts) return;
  HostEntry h;
  h.id = s_nextId++;
  h.name = name;
  h.host = host;
  h.enabled = true;
  h.reachable = false;
  h.lastLatencyMs = 0;
  s_hosts.push_back(h);
  saveHosts();
}

bool HostMonitor::removeHost(uint16_t id) {
  auto it = std::find_if(s_hosts.begin(), s_hosts.end(),
                          [id](const HostEntry &h) { return h.id == id; });
  if (it == s_hosts.end()) return false;
  s_hosts.erase(it);
  saveHosts();
  return true;
}

void HostMonitor::saveBrightness(uint8_t b) {
  s_settings.brightness = b;
  saveSettings();
}

void HostMonitor::savePageName(uint8_t pageNumber, const String &name) {
  switch (pageNumber) {
    case 2: s_settings.page2_name = name; break;
    case 3: s_settings.page3_name = name; break;
    case 4: s_settings.page4_name = name; break;
    case 5: s_settings.page5_name = name; break;
    default:
      Serial.printf("HostMonitor: savePageName ignored out-of-range page %u\n", pageNumber);
      return;
  }
  saveSettings();
}

void HostMonitor::saveInfluxConfig(const String &host, uint16_t port, const String &database,
                                    const String &username, const String &password) {
  s_settings.influx_host = host;
  s_settings.influx_port = port;
  s_settings.influx_database = database;
  s_settings.influx_username = username;
  // Empty password means "leave the existing one alone" -- lets the web UI
  // submit the form without forcing a re-entry of an already-saved password.
  if (password.length() > 0) {
    s_settings.influx_password = password;
  }
  saveSettings();
}

Settings HostMonitor::getSettings() {
  return s_settings;
}

void HostMonitor::loop() {
  uint32_t now = millis();
  if (now - s_lastPingMs < (uint32_t)s_settings.ping_interval_sec * 1000UL) return;
  s_lastPingMs = now;

  // Perform a single ping (count=1) per host, sequentially.
  for (auto &h : s_hosts) {
    if (!h.enabled) continue;
    bool ok = false;
    // Try ping by hostname/IP; Ping.ping accepts const char* or IPAddress
    ok = Ping.ping(h.host.c_str(), 1);
    h.reachable = ok;
    if (ok) {
      float avg = Ping.averageTime();
      h.lastLatencyMs = (uint32_t)avg;
    } else {
      h.lastLatencyMs = 0;
    }
  }
}

const std::vector<HostEntry>& HostMonitor::getHosts() {
  return s_hosts;
}

HostsLoadStatus HostMonitor::getHostsLoadStatus() {
  return s_hostsLoadStatus;
}