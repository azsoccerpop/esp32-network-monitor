#include <Arduino.h>
#include <WiFi.h>
#include "wifi_credentials.h"

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
