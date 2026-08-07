#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // tzapu/WiFiManager library -- note capital "WiFi",
                           // distinct from this project's own WifiManager
                           // class declared in wifi_manager.h.
#include "wifi_credentials.h"
#include "wifi_manager.h"
#include "Logger.h"

namespace {
void printWiFiStatus() {
  Serial.print("WiFi status: ");
  Serial.println(WiFi.status());
}

// The library instance that actually does the work. Kept file-local so
// nothing outside this file depends on the library directly.
WiFiManager wm;
}  // namespace

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to WiFi...");
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (millis() - start > 20000) {
      Serial.println();
      Serial.println("WiFi connection timed out.");
      return;
    }
  }

  Serial.println();
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  printWiFiStatus();
}

// --- WifiManager (captive portal) ------------------------------------------

void WifiManager::begin() {
  // Non-blocking: if saved credentials fail (or none exist), autoConnect()
  // starts the config portal and returns immediately rather than blocking
  // setup() forever -- HostMonitor/DisplayManager/WebInterface all keep
  // running via WifiManager::loop() while the portal is open.
  wm.setConfigPortalBlocking(false);

  // Portal AP name. Open (no password) for now -- add a password via
  // autoConnect("NETMON_SETUP", "somepassword") if you want to require one.
  const bool connected = wm.autoConnect("NETMON_SETUP");

  if (connected) {
    Logger::log("WifiManager: connected using saved credentials, IP: " +
                 WiFi.localIP().toString());
  } else {
    Logger::log("WifiManager: no valid saved credentials -- NETMON_SETUP "
                 "portal is open for configuration");
  }
}

void WifiManager::loop() {
  // Services the portal's captive web server when active. Cheap/no-op when
  // the portal isn't running and we're already connected.
  wm.process();
}

bool WifiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getLocalIP() {
  return WiFi.localIP().toString();
}

void WifiManager::startConfigPortal() {
  Logger::log("WifiManager: config portal manually requested");
  wm.setConfigPortalBlocking(false);
  // The main WebInterface (ESPAsyncWebServer) is already bound to port 80
  // at this point -- unlike the first-boot autoConnect() path, where it
  // runs before WebInterface starts. Starting WiFiManager's own internal
  // server on 80 here caused a port conflict severe enough to crash/reboot
  // the device. Using a different port avoids that entirely.
  wm.setHttpPort(8080);
  wm.startConfigPortal("NETMON_SETUP");
  Logger::log("WifiManager: portal server on port 8080 -- browse to "
               "http://192.168.4.1:8080 after connecting to NETMON_SETUP "
               "(auto-launch may not trigger on this port)");
}

bool WifiManager::isPortalActive() {
  return wm.getConfigPortalActive();
}
