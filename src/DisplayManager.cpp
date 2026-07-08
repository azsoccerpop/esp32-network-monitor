#include "DisplayManager.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include "HostMonitor.h"

static constexpr uint8_t kSdaPin = 21;
static constexpr uint8_t kSclPin = 22;
static constexpr uint8_t kDisplayI2cAddress = 0x3C;

// u8g2_font_helvR08_tr: Helvetica Regular 8pt, proportional sans-serif --
// closest built-in match to an "Arial" look, cleaner than a bitmap/pixel font.
// drawStr's y is the text baseline, not the top of the glyph, hence the
// offsets below rather than y=0.
static constexpr uint8_t kHeaderY = 8;          // baseline of "Network Monitor"
static constexpr uint8_t kHeaderRuleY = 11;     // horizontal rule just under the header text
static constexpr uint8_t kFirstHostY = 23;      // baseline of the first host row
static constexpr uint8_t kHostLineHeight = 12;  // vertical spacing between host rows
static constexpr uint8_t kScreenWidth = 128;
static constexpr uint8_t kScreenHeight = 64;
static constexpr uint8_t kMaxVisibleHostLines = (kScreenHeight - kFirstHostY) / kHostLineHeight + 1;
static constexpr uint32_t kPageIntervalMs = 3000;
static constexpr uint32_t kBlinkIntervalMs = 250;  // down-host rows flash at this rate

static constexpr uint8_t kNameX = 0;         // left column: host name
static constexpr uint8_t kStatusX = 62;      // middle column: UP/DOWN, fixed so it lines up across rows
static constexpr uint8_t kRightMargin = 2;    // right column: latency, right-justified against this margin
static constexpr uint8_t kNameMaxChars = 8;   // keep names clear of the status column

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
static uint8_t g_brightness = 255;
static bool s_ready = false;

static void drawHeader() {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, kHeaderY, "NetMon");

  // Right-justify the IP (or connection status) against the right margin,
  // same approach drawHostRow uses for latency, so it stays clear of the
  // label regardless of exact string width.
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

static void drawHostRow(uint8_t y, const HostEntry &h, bool blinkOn) {
  if (!h.reachable && !blinkOn) {
    // Blink "off" phase: leave this row blank so it visibly flashes.
    return;
  }

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

void DisplayManager::begin() {
  Serial.println("DisplayManager: begin");
  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(400000);

  display.setI2CAddress(kDisplayI2cAddress << 1);
  if (!display.begin()) {
    Serial.println("DisplayManager: display.begin() failed, display disabled");
    return;
  }

  s_ready = true;

  const auto s = HostMonitor::getSettings();
  g_brightness = s.brightness;
  Serial.print("Brightness set at ");
  Serial.println(g_brightness);
  display.setContrast(g_brightness);

  display.clearBuffer();
  drawHeader();
  display.sendBuffer();
}

void DisplayManager::loop() {
  if (!s_ready) return;

  const auto &hosts = HostMonitor::getHosts();

  display.clearBuffer();
  drawHeader();

  if (!hosts.empty()) {
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
  g_brightness = b;
  if (s_ready) {
    display.setContrast(g_brightness);
  }
  HostMonitor::saveBrightness(g_brightness);
}