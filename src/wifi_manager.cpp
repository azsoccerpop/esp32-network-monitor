#include <Arduino.h>
#include <WiFi.h>
#include "wifi_credentials.h"
#include "wifi_manager.h"

namespace {
void printWiFiStatus() {
  Serial.print("WiFi status: ");
  Serial.println(WiFi.status());
}
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

// --- WifiManager class stubs -----------------------------------------------
// TODO(wifimanager-migration): these are placeholders so the build links
// while DisplayManager references WifiManager::isPortalActive(). The class
// is declared in wifi_manager.h as the intended replacement for the
// hardcoded-credentials connectToWiFi() above, using the tzapu/WiFiManager
// captive-portal library (already in platformio.ini). Not implemented yet --
// tracked as a separate follow-up so it doesn't get tangled up with the
// display-corruption fix / log viewer work on this branch. Until then,
// behavior is unchanged: no portal, isPortalActive() always false.

bool WifiManager::isPortalActive() {
  return false;
}

void WifiManager::begin() {
  // Not yet implemented -- connectToWiFi() above is still what's actually
  // called from main.cpp.
}

void WifiManager::loop() {
  // Not yet implemented.
}

bool WifiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getLocalIP() {
  return WiFi.localIP().toString();
}

void WifiManager::startConfigPortal() {
  // Not yet implemented.
}

void WifiManager::loadWifiSettings() {
  // Not yet implemented.
}

void WifiManager::saveWifiSettings(const String& ssid, const String& password) {
  (void)ssid;
  (void)password;
  // Not yet implemented.
}

bool WifiManager::tryConnect() {
  return WiFi.status() == WL_CONNECTED;
}