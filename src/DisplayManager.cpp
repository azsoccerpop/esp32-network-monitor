#include "DisplayManager.h"
#include <Arduino.h>

#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include "DisplayGeometry.h"
#include "HostMonitor.h"
#include "Logger.h"
#include "NetworkMonitorPage.h"
#include "Page.h"
#include "PlaceholderPage.h"
#include "wifi_manager.h"

using namespace DisplayGeometry;

static constexpr uint8_t kSdaPin = 21;
static constexpr uint8_t kSclPin = 22;
static constexpr uint8_t kDisplayI2cAddress = 0x3C;
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

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
static uint8_t g_brightness = 255;
static bool s_ready = false;
static volatile bool s_brightnessChangePending = false;
static volatile uint8_t s_pendingBrightness = 255;
static uint32_t s_lastReinitMs = 0;

// --- Page registry -----------------------------------------------------
// Static allocation -- no dynamic memory, fixed set of pages known at
// compile time. Add new real pages by replacing a PlaceholderPage entry
// here (or appending a new slot) once they're implemented.
static NetworkMonitorPage s_networkMonitorPage;
static PlaceholderPage s_page2("Page 2");
static PlaceholderPage s_page3("Page 3");
static PlaceholderPage s_page4("Page 4");
static PlaceholderPage s_page5("Page 5");

static Page *const kPages[] = {
    &s_networkMonitorPage,
    &s_page2,
    &s_page3,
    &s_page4,
    &s_page5,
};
static constexpr size_t kNumPages = sizeof(kPages) / sizeof(kPages[0]);
static size_t s_currentPageIndex = 0;

static void drawHeader(const char *title, bool showIp) {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, kHeaderY, title);

  if (showIp) {
    char ipBuf[16] = "No WiFi";
    if (WiFi.status() == WL_CONNECTED) {
      const IPAddress ip = WiFi.localIP();
      snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    }
    const uint8_t ipW = display.getStrWidth(ipBuf);
    const uint8_t ipX = (ipW + kRightMargin < kScreenWidth) ? (kScreenWidth - kRightMargin - ipW) : 0;
    display.drawStr(ipX, kHeaderY, ipBuf);
  }

  display.drawHLine(0, kHeaderRuleY, kScreenWidth);
}

static void drawPortalPrompt() {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, 14, "WiFi setup mode:");
  display.drawStr(0, 27, "Join: NETMON_SETUP");
  display.drawStr(0, 44, "Then browse to:");
  display.drawStr(0, 57, "192.168.4.1:8080");
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

// setContrast() alone can't produce true black -- even at its minimum value
// the SH1106 still drives lit pixels at low current, so the screen stays
// faintly visible. setPowerSave(1) puts the controller in actual sleep mode
// instead, which is what "0%" on the web UI slider should mean.
static void applyBrightness(uint8_t b) {
  if (b == 0) {
    display.setPowerSave(1);
  } else {
    display.setPowerSave(0);
    display.setContrast(b);
  }
}

static void reinitDisplay() {
  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);
  display.begin();
  applyBrightness(g_brightness);
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
  applyBrightness(g_brightness);

  s_page2.setName(s.page2_name);
  s_page3.setName(s.page3_name);
  s_page4.setName(s.page4_name);
  s_page5.setName(s.page5_name);

  kPages[s_currentPageIndex]->onSelect();

  display.clearBuffer();
  drawHeader(kPages[s_currentPageIndex]->name(), kPages[s_currentPageIndex]->showIpInHeader());
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
    applyBrightness(g_brightness);
  }

  if (s_brightnessChangePending) {
    s_brightnessChangePending = false;
    g_brightness = s_pendingBrightness;
    applyBrightness(g_brightness);
  }

  display.clearBuffer();

  if (WifiManager::isPortalActive()) {
    drawHeader("Setup", false);
    drawPortalPrompt();
  } else {
    Page *page = kPages[s_currentPageIndex];
    drawHeader(page->name(), page->showIpInHeader());
    page->draw(display);
  }

  display.sendBuffer();
}

void DisplayManager::setBrightness(uint8_t b) {
  s_pendingBrightness = b;
  s_brightnessChangePending = true;
  HostMonitor::saveBrightness(b);
}

void DisplayManager::nextPage() {
  s_currentPageIndex = (s_currentPageIndex + 1) % kNumPages;
  kPages[s_currentPageIndex]->onSelect();
  Logger::log(String("DisplayManager: switched to page '") + kPages[s_currentPageIndex]->name() + "'");
}

void DisplayManager::previousPage() {
  s_currentPageIndex = (s_currentPageIndex + kNumPages - 1) % kNumPages;
  kPages[s_currentPageIndex]->onSelect();
  Logger::log(String("DisplayManager: switched to page '") + kPages[s_currentPageIndex]->name() + "'");
}

const char *DisplayManager::currentPageName() {
  return kPages[s_currentPageIndex]->name();
}

void DisplayManager::setPageName(uint8_t pageNumber, const String &name) {
  switch (pageNumber) {
    case 2: s_page2.setName(name); break;
    case 3: s_page3.setName(name); break;
    case 4: s_page4.setName(name); break;
    case 5: s_page5.setName(name); break;
    default:
      Logger::log("DisplayManager: setPageName ignored out-of-range page " + String(pageNumber));
      return;
  }
  Logger::log("DisplayManager: page " + String(pageNumber) + " renamed to '" + name + "'");
}
