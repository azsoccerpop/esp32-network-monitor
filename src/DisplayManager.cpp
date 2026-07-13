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
// Periodically re-run full controller init. This is a self-healing measure:
// if I2C noise ever corrupts the SH1106's internal state (e.g. its
// orientation registers), this recovers it without needing a manual reboot.
static constexpr uint32_t kReinitIntervalMs = 5UL * 60UL * 1000UL;  // 5 minutes

static constexpr uint8_t kNameX = 0;         // left column: host name
static constexpr uint8_t kStatusX = 62;      // middle column: UP/DOWN, fixed so it lines up across rows
static constexpr uint8_t kRightMargin = 2;    // right column: latency, right-justified against this margin
static constexpr uint8_t kNameMaxChars = 8;   // keep names clear of the status column

// U8G2_R0 = no rotation -- confirmed correct for this display's physical
// mounting once the I2C corruption issue was resolved. If it's ever wrong
// again, swap to U8G2_R2 (180°) rather than assuming it's another
// corruption episode.
static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
static uint8_t g_brightness = 255;
static bool s_ready = false;
// setBrightness() can be called from WebInterface's async request-handler
// task, which runs concurrently with the main loop() task. u8g2 (and the
// underlying I2C bus) isn't safe to drive from two tasks at once, so the
// actual display.setContrast() call is deferred to DisplayManager::loop()
// via this flag rather than happening inline in setBrightness().
static volatile bool s_brightnessChangePending = false;
static volatile uint8_t s_pendingBrightness = 255;
static uint32_t s_lastReinitMs = 0;

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

// Shown in place of the host list when there's nothing to display -- either
// hosts.json is genuinely empty, or it failed to load. Distinguishing the
// two matters: an empty list is a normal "add a host" state, while a load
// failure means something's actually broken and needs attention.
static void drawEmptyHostsMessage() {
  display.setFont(u8g2_font_helvR08_tr);

  const char *line1;
  const char *line2;
  switch (HostMonitor::getHostsLoadStatus()) {
    case HostsLoadStatus::FileNotFound:
      line1 = "Host list error";
      line2 = "hosts.json missing";
      break;
    case HostsLoadStatus::ParseError:
      line1 = "Host list error";
      line2 = "hosts.json invalid";
      break;
    case HostsLoadStatus::Ok:
    default:
      line1 = "No hosts configured";
      line2 = "Add via web UI";
      break;
  }

  display.drawStr(kNameX, kFirstHostY, line1);
  display.drawStr(kNameX, kFirstHostY + kHostLineHeight, line2);
}

void DisplayManager::begin() {
  Serial.println("DisplayManager: begin");
  Wire.begin(kSdaPin, kSclPin);
  // Dropped from 400kHz -- more tolerant of noisy/long I2C wiring, which is
  // the leading suspect for display corruption occurring mid-session. Bump
  // back to 400000 once wiring/interference is ruled out, if you want the
  // faster refresh.
  Wire.setClock(100000);

  display.setI2CAddress(kDisplayI2cAddress << 1);
  if (!display.begin()) {
    Serial.println("DisplayManager: display.begin() failed, display disabled");
    return;
  }

  s_ready = true;
  s_lastReinitMs = millis();

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

  const uint32_t now = millis();
  if (now - s_lastReinitMs >= kReinitIntervalMs) {
    s_lastReinitMs = now;
    Serial.println("DisplayManager: periodic reinit");
    display.begin();
    display.setContrast(g_brightness);
  }

  if (s_brightnessChangePending) {
    s_brightnessChangePending = false;
    g_brightness = s_pendingBrightness;
    display.setContrast(g_brightness);
  }

  const auto &hosts = HostMonitor::getHosts();

  display.clearBuffer();
  drawHeader();

  if (hosts.empty()) {
    drawEmptyHostsMessage();
  } else {
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