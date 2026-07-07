#include "WebInterface.h"
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "HostMonitor.h"
#include "DisplayManager.h"

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

void WebInterface::begin() {
  Serial.println("WebInterface: begin");

  serveStaticFiles();

  server.on("/api/hosts", HTTP_GET, handleGetHosts);
  server.on("/api/hosts", HTTP_DELETE, handleDeleteHost);
  server.on("/api/settings", HTTP_GET, handleGetSettings);

  auto *hostsPost = new AsyncCallbackJsonWebHandler("/api/hosts", handlePostHost);
  hostsPost->setMethod(HTTP_POST);
  server.addHandler(hostsPost);

  auto *settingsPost = new AsyncCallbackJsonWebHandler("/api/settings", handlePostSettings);
  settingsPost->setMethod(HTTP_POST);
  server.addHandler(settingsPost);

  server.begin();
  Serial.println("WebInterface: server started on port 80");
}

void WebInterface::loop() {
  // nothing to do here for AsyncWebServer
}