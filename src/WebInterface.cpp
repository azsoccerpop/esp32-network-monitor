#include "WebInterface.h"
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ElegantOTA.h>
#include "HostMonitor.h"
#include "DisplayManager.h"
#include "Logger.h"
#include "wifi_manager.h"
#include "InfluxClient.h"
#include "PageConfigStore.h"
#include "MetricsManager.h"

static AsyncWebServer server(80);

static void serveStaticFiles() {
  Serial.println("WebInterface: serving static files");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!LittleFS.exists("/index.html")) {
      request->send(404, "text/plain", "index.html not found");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
}

static void handleGetHosts(AsyncWebServerRequest *request) {
  const auto &hosts = HostMonitor::getHosts();
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &h : hosts) {
    JsonObject o = arr.createNestedObject();
    o["id"] = h.id;
    o["name"] = h.name;
    o["host"] = h.host;
    o["enabled"] = h.enabled;
    o["reachable"] = h.reachable;
    o["lastLatencyMs"] = h.lastLatencyMs;
  }
  String out;
  serializeJson(arr, out);
  request->send(200, "application/json", out);
}

static void handlePostHost(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  const char *name = obj["name"] | "";
  const char *host = obj["host"] | "";
  if (strlen(host) == 0) {
    request->send(400, "application/json", "{\"error\":\"host required\"}");
    return;
  }
  HostMonitor::addHost(String(name), String(host));
  request->send(200, "application/json", "{\"ok\":true}");
}

static void handleDeleteHost(AsyncWebServerRequest *request) {
  if (!request->hasParam("id")) {
    request->send(400, "application/json", "{\"error\":\"id required\"}");
    return;
  }
  const uint16_t id = static_cast<uint16_t>(request->getParam("id")->value().toInt());
  if (!HostMonitor::removeHost(id)) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }
  request->send(200, "application/json", "{\"ok\":true}");
}

static void handleGetSettings(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);
  const auto s = HostMonitor::getSettings();
  doc["brightness"] = s.brightness;
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void handlePostSettings(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  if (!obj.containsKey("brightness")) {
    request->send(400, "application/json", "{\"error\":\"brightness required\"}");
    return;
  }
  uint8_t brightness = obj["brightness"] | 255;
  DisplayManager::setBrightness(brightness);
  request->send(200, "application/json", "{\"ok\":true}");
}

static void handleGetPageConfig(AsyncWebServerRequest *request) {
  if (!request->hasArg("page")) {
    request->send(400, "application/json", "{\"error\":\"page query param required\"}");
    return;
  }
  const uint8_t pageNumber = request->arg("page").toInt();
  if (pageNumber < 2 || pageNumber > 5) {
    request->send(400, "application/json", "{\"error\":\"page must be 2-5\"}");
    return;
  }
  const PageConfig cfg = PageConfigStore::getPageConfig(pageNumber);
  DynamicJsonDocument doc(1024);
  doc["name"] = cfg.name;
  doc["format"] = PageConfigStore::formatToString(cfg.format);
  JsonArray widgets = doc.createNestedArray("widgets");
  for (const auto &w : cfg.widgets) {
    JsonObject wo = widgets.createNestedObject();
    wo["label"] = w.label;
    wo["query"] = w.query;
    wo["field"] = w.field;
  }
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void handlePostPageMeta(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  const uint8_t pageNumber = obj["page"] | 0;
  const char *name = obj["name"] | "";
  const char *format = obj["format"] | "table";
  if (pageNumber < 2 || pageNumber > 5) {
    request->send(400, "application/json", "{\"error\":\"page must be 2-5\"}");
    return;
  }
  if (strlen(name) == 0) {
    request->send(400, "application/json", "{\"error\":\"name required\"}");
    return;
  }
  // Keep it short -- shown in a 128px-wide OLED header alongside a rule
  // line, no IP to share space with on these pages, but still tight.
  String nameStr(name);
  if (nameStr.length() > 16) {
    nameStr = nameStr.substring(0, 16);
  }
  PageConfigStore::savePageMeta(pageNumber, nameStr, PageConfigStore::formatFromString(format));
  DisplayManager::reloadPageConfig(pageNumber);
  request->send(200, "application/json", "{\"ok\":true,\"name\":\"" + nameStr + "\"}");
}

static void handlePostPageWidgets(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  const uint8_t pageNumber = obj["page"] | 0;
  if (pageNumber < 2 || pageNumber > 5) {
    request->send(400, "application/json", "{\"error\":\"page must be 2-5\"}");
    return;
  }
  JsonArray widgetsArr = obj["widgets"];
  if (widgetsArr.isNull() || widgetsArr.size() != 4) {
    request->send(400, "application/json", "{\"error\":\"widgets must be an array of exactly 4\"}");
    return;
  }
  MetricWidget widgets[4];
  uint8_t i = 0;
  for (JsonObject wo : widgetsArr) {
    widgets[i].label = String(wo["label"] | "");
    widgets[i].query = String(wo["query"] | "");
    widgets[i].field = String(wo["field"] | "");
    ++i;
  }
  PageConfigStore::saveWidgets(pageNumber, widgets);
  // Poll immediately so the OLED/web UI show fresh values right away rather
  // than waiting up to a minute for the next scheduled background poll.
  MetricsManager::refreshNow();
  Logger::log("WebInterface: page " + String(pageNumber) + " widgets updated");
  request->send(200, "application/json", "{\"ok\":true}");
}

static void handleGetInflux(AsyncWebServerRequest *request) {
  const auto s = HostMonitor::getSettings();
  DynamicJsonDocument doc(512);
  doc["version"] = s.influx_version;
  doc["host"] = s.influx_host;
  doc["port"] = s.influx_port;
  doc["database"] = s.influx_database;
  doc["username"] = s.influx_username;
  // Never send the actual password back to the browser -- only whether one
  // is currently set, so the web UI can show a placeholder instead of the
  // real value.
  doc["passwordSet"] = s.influx_password.length() > 0;
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void handlePostInflux(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!json.is<JsonObject>()) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  JsonObject obj = json.as<JsonObject>();
  const char *host = obj["host"] | "";
  const uint16_t port = obj["port"] | 8086;
  const char *database = obj["database"] | "";
  const char *username = obj["username"] | "";
  // Empty password means "leave whatever's already stored alone" -- see
  // HostMonitor::saveInfluxConfig().
  const char *password = obj["password"] | "";

  HostMonitor::saveInfluxConfig(String(host), port, String(database), String(username), String(password));
  Logger::log("WebInterface: InfluxDB connection settings updated (host: " + String(host) + ")");
  request->send(200, "application/json", "{\"ok\":true}");
}

// Blocking (HTTP request(s) to the InfluxDB server) -- fine here since this
// runs on the AsyncWebServer's own task, not the main loop() that
// DisplayManager's I2C timing depends on. Tests whatever is currently
// persisted, so the web UI always saves first, then calls this.
static void handlePostInfluxTest(AsyncWebServerRequest *request) {
  const auto s = HostMonitor::getSettings();
  const InfluxTestResult result = testInfluxConnection(
      s.influx_host, s.influx_port, s.influx_database, s.influx_username, s.influx_password);

  Logger::log("WebInterface: InfluxDB test -- " + result.message);

  DynamicJsonDocument doc(1024);
  doc["reachable"] = result.reachable;
  doc["authOk"] = result.authOk;
  doc["databaseFound"] = result.databaseFound;
  doc["version"] = result.influxVersion;
  doc["message"] = result.message;
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void handleGetLogs(AsyncWebServerRequest *request) {
  request->send(200, "application/json", Logger::getLogsJson());
}

static void handlePostWifiReset(AsyncWebServerRequest *request) {
  Logger::log("WebInterface: WiFi reset requested via web UI, opening NETMON_SETUP portal");
  WifiManager::startConfigPortal();
  request->send(200, "application/json", "{\"ok\":true}");
}

// Page navigation controls, ahead of the physical rotary encoder existing.
// Kept as a permanent option even once the encoder is wired up (not just a
// temporary stand-in) -- useful for testing/manual override either way.
static void handlePostDisplayNextPage(AsyncWebServerRequest *request) {
  DisplayManager::nextPage();
  request->send(200, "application/json", "{\"page\":\"" + String(DisplayManager::currentPageName()) + "\"}");
}

static void handlePostDisplayPrevPage(AsyncWebServerRequest *request) {
  DisplayManager::previousPage();
  request->send(200, "application/json", "{\"page\":\"" + String(DisplayManager::currentPageName()) + "\"}");
}

void WebInterface::begin() {
  Serial.println("WebInterface: begin");

  serveStaticFiles();

  server.on("/api/hosts", HTTP_GET, handleGetHosts);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/hosts", HTTP_DELETE, handleDeleteHost);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/page-config", HTTP_GET, handleGetPageConfig);
  server.on("/api/influx", HTTP_GET, handleGetInflux);
  server.on("/api/influx/test", HTTP_POST, handlePostInfluxTest);
  server.on("/api/wifi/reset", HTTP_POST, handlePostWifiReset);
  server.on("/api/display/next-page", HTTP_POST, handlePostDisplayNextPage);
  server.on("/api/display/prev-page", HTTP_POST, handlePostDisplayPrevPage);

  auto *hostsPost = new AsyncCallbackJsonWebHandler("/api/hosts", handlePostHost);
  hostsPost->setMethod(HTTP_POST);
  server.addHandler(hostsPost);

  auto *settingsPost = new AsyncCallbackJsonWebHandler("/api/settings", handlePostSettings);
  settingsPost->setMethod(HTTP_POST);
  server.addHandler(settingsPost);

  auto *pageMetaPost = new AsyncCallbackJsonWebHandler("/api/page-meta", handlePostPageMeta);
  pageMetaPost->setMethod(HTTP_POST);
  server.addHandler(pageMetaPost);

  auto *pageWidgetsPost = new AsyncCallbackJsonWebHandler("/api/page-widgets", handlePostPageWidgets);
  pageWidgetsPost->setMethod(HTTP_POST);
  server.addHandler(pageWidgetsPost);

  auto *influxPost = new AsyncCallbackJsonWebHandler("/api/influx", handlePostInflux);
  influxPost->setMethod(HTTP_POST);
  server.addHandler(influxPost);

  // Registers GET/POST handlers at /update for firmware & filesystem OTA
  // uploads, using its own bundled UI. Uncomment setAuth() below to require
  // a login before an upload is accepted.
  ElegantOTA.begin(&server);
  // ElegantOTA.setAuth("admin", "changeme");

  server.begin();
  Serial.println("WebInterface: server started on port 80");
}

void WebInterface::loop() {
  ElegantOTA.loop();
}