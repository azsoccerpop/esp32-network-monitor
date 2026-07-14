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

// ... (rest of original functions: drawHostRow, drawEmptyHostsMessage, begin, loop, setBrightness) ...

void DisplayManager::loop() {
  if (!s_ready) return;

  // ... original reinit and brightness code ...

  display.clearBuffer();
  drawHeader();

  if (WifiManager::isPortalActive()) {
    drawPortalPrompt();
  } else if (HostMonitor::getHosts().empty()) {
    drawEmptyHostsMessage();
  } else {
    // original host drawing code
  }

  display.sendBuffer();
}