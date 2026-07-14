#pragma once
#include <Arduino.h>

class WifiManager {
public:
  static void begin();
  static void loop();
  static bool isConnected();
  static String getLocalIP();
  static void startConfigPortal();
  static bool isPortalActive();   // NEW: for DisplayManager
private:
  static void loadWifiSettings();
  static void saveWifiSettings(const String& ssid, const String& password);
  static bool tryConnect();
};
