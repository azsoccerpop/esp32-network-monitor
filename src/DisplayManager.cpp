#include "DisplayManager.h"
#include <Arduino.h>

#include <LittleFS.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "HostMonitor.h"

static constexpr uint8_t kSdaPin = 21;
static constexpr uint8_t kSclPin = 22;
static constexpr uint8_t kDisplayI2cAddress = 0x3C;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
static uint8_t g_brightness = 128;

void DisplayManager::begin() {
  Serial.println("DisplayManager: begin");
  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(400000);

  uint8_t detectedAddress = 0;
  for (uint8_t candidate : {0x3C, 0x3D}) {
    Wire.beginTransmission(candidate);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      detectedAddress = candidate;
      break;
    }
  }

  if (detectedAddress == 0) {
    Serial.println("I2C OLED not detected at common SH1106 addresses 0x3C or 0x3D");
    return;
  }

  Serial.print("I2C OLED detected at 0x");
  Serial.println(detectedAddress, HEX);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed in DisplayManager");
  }

  display.setI2CAddress(detectedAddress);
  display.begin();
  auto s = HostMonitor::getSettings();
  g_brightness = s.brightness;
  display.setContrast(g_brightness);
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 0, "Network Monitor");
  display.sendBuffer();
}

void DisplayManager::loop() {
  const auto &hosts = HostMonitor::getHosts();
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 0, "Network Monitor");
  if (!hosts.empty()) {
    auto &h = hosts[0];
    String line = h.name + ": " + (h.reachable ? "UP" : "DOWN");
    display.drawStr(0, 14, line.c_str());
    String lat = "lat:" + String(h.lastLatencyMs) + "ms";
    display.drawStr(0, 30, lat.c_str());
  }
  display.sendBuffer();
}

void DisplayManager::setBrightness(uint8_t b) {
  g_brightness = b;
  display.setContrast(g_brightness);
  HostMonitor::saveBrightness(g_brightness);
}
