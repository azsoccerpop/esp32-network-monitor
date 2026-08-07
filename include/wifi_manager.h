#pragma once
#include <Arduino.h>

// Wraps tzapu/WiFiManager to provide:
//  - automatic connect using previously-saved credentials (stored by the
//    library itself in NVS -- wifi_credentials.h and the old hardcoded
//    connect path have been removed, no longer needed)
//  - if no saved credentials, or they fail, opens a captive-portal access
//    point named NETMON_SETUP that any phone/laptop can join to configure
//    WiFi via a browser page (auto-launched on most devices)
//  - runs non-blocking, so DisplayManager/WebInterface/HostMonitor keep
//    running normally while the portal is open
class WifiManager {
public:
  // Call once from setup(). Attempts to connect using saved credentials; if
  // that fails, starts the NETMON_SETUP captive portal (non-blocking).
  static void begin();

  // Call every loop() iteration. Services the captive portal's web server
  // when active; a no-op otherwise.
  static void loop();

  static bool isConnected();
  static String getLocalIP();

  // Forces the captive portal open even if currently connected -- e.g. from
  // a "reconfigure WiFi" button in the web UI.
  static void startConfigPortal();

  // True while the NETMON_SETUP captive portal is active and waiting for
  // configuration. DisplayManager uses this to show the portal prompt
  // instead of the normal host list.
  static bool isPortalActive();
};
