#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // tzapu/WiFiManager library -- note capital "WiFi",
                           // distinct from this project's own WifiManager
                           // class declared in wifi_manager.h.
#include "wifi_manager.h"
#include "Logger.h"

namespace {
// Tracks portal active/inactive transitions so WifiManager::loop() can log
// exactly once when it closes, rather than every loop iteration.
bool s_wasPortalActive = false;

// The library instance that actually does the work. Kept file-local so
// nothing outside this file depends on the library directly.
WiFiManager wm;
}  // namespace

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

  // The web UI is unreachable for the whole time the portal is open (no
  // WiFi to reach it over), so there's no way to watch this live -- but the
  // log buffer survives the WiFi mode switch (no reboot happens here), so
  // logging the outcome here means it's waiting for you once the device
  // reconnects and the web UI comes back.
  const bool active = wm.getConfigPortalActive();
  if (s_wasPortalActive && !active) {
    if (WiFi.status() == WL_CONNECTED) {
      Logger::log("WifiManager: portal closed, connected to new network, IP: " +
                   WiFi.localIP().toString());
    } else {
      Logger::log("WifiManager: portal closed without a successful connection "
                   "(timed out or cancelled)");
    }
  }
  s_wasPortalActive = active;
}

bool WifiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getLocalIP() {
  return WiFi.localIP().toString();
}

void WifiManager::startConfigPortal() {
  Logger::log("WifiManager: WiFi reset requested -- clearing saved credentials before opening portal");

  // Two separate layers persist credentials on ESP32, and both need
  // clearing or the device silently falls back to the known network instead
  // of actually waiting in the portal:
  //  1. WiFiManager's own saved config (its NVS namespace)
  //  2. The ESP32 radio's own internal persistence (WiFi.persistent(true)
  //     is on by default in Arduino-ESP32, independent of WiFiManager) --
  //     this is what caused the very first portal test to silently
  //     reconnect on its own too.
  wm.resetSettings();
  WiFi.disconnect(/*wifioff=*/true, /*eraseap=*/true);
  delay(100);

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
