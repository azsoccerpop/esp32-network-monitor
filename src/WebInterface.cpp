#include "WebInterface.h"
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "HostMonitor.h"

static AsyncWebServer server(80);

void serveStaticFiles() {
  Serial.println("WebInterface: mounting LittleFS");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }

  Serial.println("WebInterface: serving static files");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
      request->send(404, "text/plain", "index.html not found");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "application/css");
  });
}

void handleApiHosts(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
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
    return;
  }

  if (request->method() == HTTP_POST) {
    // Add host (simple implementation)
    if (request->hasParam("body", true)) {
      String body = request->getParam("body", true)->value();
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
      }
      const char *name = doc["name"] | "";
      const char *host = doc["host"] | "";
      if (strlen(host) == 0) {
        request->send(400, "application/json", "{\"error\":\"host required\"}");
        return;
      }
      HostMonitor::addHost(String(name), String(host));
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    request->send(400);
  }
}

void handleApiSettings(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
    DynamicJsonDocument doc(1024);
    auto s = HostMonitor::getSettings();
    doc["brightness"] = s.brightness;
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
    return;
  }

  if (request->method() == HTTP_POST) {
    HostMonitor::saveBrightness(200);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
}

void WebInterface::begin() {
  Serial.println("WebInterface: begin");
  serveStaticFiles();

  server.on("/api/hosts", HTTP_ANY, [](AsyncWebServerRequest *r){ handleApiHosts(r); });
  server.on("/api/settings", HTTP_ANY, [](AsyncWebServerRequest *r){ handleApiSettings(r); });

  server.begin();
  Serial.println("WebInterface: server started on port 80");
}

void WebInterface::loop() {
  // nothing to do here for AsyncWebServer
}
