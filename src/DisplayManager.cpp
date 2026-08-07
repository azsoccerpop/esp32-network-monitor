#include "DisplayManager.h"
#include <Arduino.h>

#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include "HostMonitor.h"
#include "Logger.h"
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

// --- I2C bus health / recovery ---------------------------------------------
// If electrical noise corrupts an I2C transaction at the wrong moment, a
// device can be left holding SDA low, wedging the bus permanently -- no
// future transaction (including display.begin() re-inits) can succeed until
// something manually clocks SCL to force the stuck device to release SDA.
// This is what was causing corruption to "never self-correct" without a long
// unplug: only fully removing power let the OLED's own capacitors discharge
// enough to reset it. We now detect this automatically and recover in
// seconds instead of requiring a manual power cycle.
static constexpr uint32_t kHealthCheckIntervalMs = 2000;
static constexpr uint8_t kConsecutiveFailuresBeforeRecovery = 3;
static uint32_t s_lastHealthCheckMs = 0;
static uint8_t s_consecutiveFailures = 0;

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

// Bit-bangs up to 9 SCL clock pulses (the max an I2C slave could ever be
// waiting on mid-byte) to coax a wedged slave into releasing SDA, then
// issues a manual STOP condition. This is the standard I2C bus recovery
// routine; see NXP's I2C-bus specification, section on bus recovery.
// Returns true if both lines read high (bus idle) afterward.
static bool recoverI2CBus(uint8_t sdaPin, uint8_t sclPin) {
  Wire.end();

  pinMode(sclPin, OUTPUT);
  pinMode(sdaPin, INPUT_PULLUP);
  digitalWrite(sclPin, HIGH);
  delayMicroseconds(5);

  for (uint8_t i = 0; i < 9; ++i) {
    if (digitalRead(sdaPin) == HIGH) break;  // slave released SDA, done clocking
    digitalWrite(sclPin, LOW);
    delayMicroseconds(5);
    digitalWrite(sclPin, HIGH);
    delayMicroseconds(5);
  }

  // Manually issue a STOP condition: SDA low-to-high while SCL is high.
  pinMode(sdaPin, OUTPUT);
  digitalWrite(sdaPin, LOW);
  delayMicroseconds(5);
  digitalWrite(sclPin, HIGH);
  delayMicroseconds(5);
  digitalWrite(sdaPin, HIGH);
  delayMicroseconds(5);

  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  delayMicroseconds(5);

  return digitalRead(sdaPin) == HIGH && digitalRead(sclPin) == HIGH;
}

// Cheap I2C health check: a zero-length transmission just tests whether the
// display ACKs its address. No corrupted-content risk (unlike sendBuffer())
// and cheap enough to run every couple seconds without hurting frame rate.
static bool isDisplayResponding() {
  Wire.beginTransmission(kDisplayI2cAddress);
  return Wire.endTransmission() == 0;
}

static void reinitDisplay() {
  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);
  display.begin();
  display.setContrast(g_brightness);
}

void DisplayManager::begin() {
  Serial.println("DisplayManager: begin");
  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);

  display.setI2CAddress(kDisplayI2cAddress << 1);
  if (!display.begin()) {
    Logger::log("DisplayManager: display.begin() FAILED at startup -- display will stay blank. "
                "Attempting one bus recovery + retry.");
    const bool recovered = recoverI2CBus(kSdaPin, kSclPin);
    Wire.begin(kSdaPin, kSclPin);
    Wire.setClock(100000);
    display.setI2CAddress(kDisplayI2cAddress << 1);
    if (!recovered || !display.begin()) {
      Logger::log("DisplayManager: retry after recovery also FAILED. Display disabled; "
                   "will not attempt to draw. Check wiring/power -- this is not something "
                   "software can recover from if the display never ACKs at all.");
      return;
    }
    Logger::log("DisplayManager: recovered and initialized successfully on retry.");
  }

  s_ready = true;
  s_lastReinitMs = millis();
  s_lastHealthCheckMs = millis();

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

  // Fast fault detection: check the bus is actually responding every couple
  // seconds. If it's failed several checks in a row (a single blip can be
  // transient noise, so we don't overreact to one failure), the bus is
  // almost certainly wedged -- recover it immediately rather than waiting
  // for the slow periodic reinit below, which can't fix a stuck bus anyway
  // since it just re-sends commands over an already-jammed bus.
  if (now - s_lastHealthCheckMs >= kHealthCheckIntervalMs) {
    s_lastHealthCheckMs = now;
    if (isDisplayResponding()) {
      s_consecutiveFailures = 0;
    } else {
      ++s_consecutiveFailures;
      Logger::log("DisplayManager: I2C health check failed (" +
                   String(s_consecutiveFailures) + " consecutive)");
      if (s_consecutiveFailures >= kConsecutiveFailuresBeforeRecovery) {
        Logger::log("DisplayManager: I2C bus appears stuck, attempting recovery");
        const bool recovered = recoverI2CBus(kSdaPin, kSclPin);
        Logger::log(String("DisplayManager: bus recovery ") + (recovered ? "succeeded" : "FAILED") +
                     ", reinitializing display");
        reinitDisplay();
        s_lastReinitMs = now;
        s_consecutiveFailures = 0;
      }
    }
  }

  if (now - s_lastReinitMs >= kReinitIntervalMs) {
    s_lastReinitMs = now;
    Logger::log("DisplayManager: periodic reinit");
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