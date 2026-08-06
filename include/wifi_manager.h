#pragma once
#include <Arduino.h>

void connectToWiFi();

class WifiManager {
public:
  static void begin();
  static void loop();
  static bool isConnected();
  static String getLocalIP();
  static void startConfigPortal();
  static bool isPortalActive();   // used by DisplayManager
private:
  static void loadWifiSettings();
  static void saveWifiSettings(const String& ssid, const String& password);
  static bool tryConnect();
};