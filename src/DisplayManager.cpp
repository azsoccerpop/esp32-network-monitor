#include "DisplayManager.h"
#include <Arduino.h>

#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include "HostMonitor.h"
#include "wifi_manager.h"

static constexpr uint8_t kSdaPin = 21;
static constexpr uint8_t kSclPin = 22;
static constexpr uint8_t kDisplayI2cAddress = 0x3C;

// Font and layout constants (same as original)
static constexpr uint8_t kHeaderY = 8;
static constexpr uint8_t kHeaderRuleY = 11;
static constexpr uint8_t kFirstHostY = 23;
static constexpr uint8_t kHostLineHeight = 12;
static constexpr uint8_t kScreenWidth = 128;
static constexpr uint8_t kScreenHeight = 64;
static constexpr uint8_t kMaxVisibleHostLines = (kScreenHeight - kFirstHostY) / kHostLineHeight + 1;
static constexpr uint32_t kPageIntervalMs = 3000;
static constexpr uint32_t kBlinkIntervalMs = 250;
static constexpr uint32_t kReinitIntervalMs = 5UL * 60UL * 1000UL;

static constexpr uint8_t kNameX = 0;
static constexpr uint8_t kStatusX = 62;
static constexpr uint8_t kRightMargin = 2;
static constexpr uint8_t kNameMaxChars = 8;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
static uint8_t g_brightness = 255;
static bool s_ready = false;
static volatile bool s_brightnessChangePending = false;
static volatile uint8_t s_pendingBrightness = 255;
static uint32_t s_lastReinitMs = 0;

static void drawHeader() {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, kHeaderY, "NetMon");

  char ipBuf[16] = "No WiFi";
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }
  const uint8_t ipW = display.getStrWidth(ipBuf);
  const uint8_t ipX = (ipW + kRightMargin < kScreenWidth) ? (kScreenWidth - kRightMargin - ipW) : 0;
  display.drawStr(ipX, kHeaderY, ipBuf);

  display.drawHLine(0, kHeaderRuleY, kScreenWidth);
}

static void drawPortalPrompt() {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, 20, "Connect to:");
  display.drawStr(0, 32, "ESP-NetMon-Setup");
  display.drawStr(0, 44, "Open browser");
  display.drawStr(0, 56, "to configure");
}

static void drawHostRow(uint8_t y, const HostEntry &h, bool blinkOn) {
  if (!h.reachable && !blinkOn) return;

  display.setFont(h.reachable ? u8g2_font_helvR08_tr : u8g2_font_helvB08_tr);

  char nameBuf[16];
  snprintf(nameBuf, sizeof(nameBuf), "%.*s", kNameMaxChars, h.name.c_str());
  display.drawStr(kNameX, y, nameBuf);

  display.drawStr(kStatusX, y, h.reachable ? "UP" : "DOWN");

  if (h.reachable) {
    char latBuf[16];
    snprintf(latBuf, sizeof(latBuf), "%lums", static_cast<unsigned long>(h.lastLatencyMs));
    const uint8_t w = display.getStrWidth(latBuf);
    const uint8_t x = (w + kRightMargin < kScreenWidth) ? (kScreenWidth - kRightMargin - w) : 0;
    display.drawStr(x, y, latBuf);
  }
}

static void drawEmptyHostsMessage() {
  display.setFont(u8g2_font_helvR08_tr);
  const char *line1 = "No hosts configured";
  const char *line2 = "Add via web UI";
  display.drawStr(kNameX, kFirstHostY, line1);
  display.drawStr(kNameX, kFirstHostY + kHostLineHeight, line2);
}

void DisplayManager::begin() {
  Serial.println("DisplayManager: begin");
  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);

  display.setI2CAddress(kDisplayI2cAddress << 1);
  if (!display.begin()) {
    Serial.println("DisplayManager: display.begin() failed");
    return;
  }

  s_ready = true;
  s_lastReinitMs = millis();

  const auto s = HostMonitor::getSettings();
  g_brightness = s.brightness;
  display.setContrast(g_brightness);

  display.clearBuffer();
  drawHeader();
  display.sendBuffer();
}

void DisplayManager::loop() {
  if (!s_ready) return;

  const uint32_t now = millis();
  if (now - s_lastReinitMs >= kReinitIntervalMs) {
    s_lastReinitMs = now;
    display.begin();
    display.setContrast(g_brightness);
  }

  if (s_brightnessChangePending) {
    s_brightnessChangePending = false;
    g_brightness = s_pendingBrightness;
    display.setContrast(g_brightness);
  }

  display.clearBuffer();
  drawHeader();

  if (WifiManager::isPortalActive()) {
    drawPortalPrompt();
  } else if (HostMonitor::getHosts().empty()) {
    drawEmptyHostsMessage();
  } else {
    const auto &hosts = HostMonitor::getHosts();
    const size_t numHosts = hosts.size();
    const size_t numPages = (numHosts + kMaxVisibleHostLines - 1) / kMaxVisibleHostLines;
    const size_t page = (numPages > 1) ? (millis() / kPageIntervalMs) % numPages : 0;
    const size_t startIdx = page * kMaxVisibleHostLines;
    const size_t endIdx = min(startIdx + kMaxVisibleHostLines, numHosts);
    const bool blinkOn = ((millis() / kBlinkIntervalMs) % 2) == 0;

    uint8_t row = 0;
    for (size_t i = startIdx; i < endIdx; ++i, ++row) {
      drawHostRow(kFirstHostY + row * kHostLineHeight, hosts[i], blinkOn);
    }
  }

  display.sendBuffer();
}

void DisplayManager::setBrightness(uint8_t b) {
  s_pendingBrightness = b;
  s_brightnessChangePending = true;
  HostMonitor::saveBrightness(b);
}
