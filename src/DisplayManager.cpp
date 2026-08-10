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
#include "MetricsPage.h"
#include "PageConfigStore.h"
#include "RotaryEncoder.h"
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
// compile time. Add new real pages by replacing a MetricsPage entry
// here (or appending a new slot) once they're implemented.
static NetworkMonitorPage s_networkMonitorPage;
static MetricsPage s_page2(2);
static MetricsPage s_page3(3);
static MetricsPage s_page4(4);
static MetricsPage s_page5(5);

static Page *const kPages[] = {
    &s_networkMonitorPage,
    &s_page2,
    &s_page3,
    &s_page4,
    &s_page5,
};
static constexpr size_t kNumPages = sizeof(kPages) / sizeof(kPages[0]);
static size_t s_currentPageIndex = 0;

// --- Rotary encoder / settings (brightness) mode ---------------------------
// Pressing the encoder's button toggles a dedicated brightness screen,
// separate from the normal page rotation -- rotating while it's showing
// adjusts brightness instead of switching pages. Pressing the button again
// always returns to the Network page specifically (not "whatever page was
// showing before"), per how this was specified.
static bool s_settingsMode = false;
static constexpr int kBrightnessStepPerDetent = 10;  // out of 255

// Flash writes are relatively slow and LittleFS wears with repeated writes,
// so spinning the knob doesn't save to disk on every single detent --
// brightness is applied live immediately (visual feedback has no delay),
// but the persisted save is debounced until rotation has been idle for a
// bit, and always flushed when leaving settings mode so a change is never
// lost even if the idle window hasn't elapsed yet.
static constexpr uint32_t kBrightnessSaveDebounceMs = 500;
static bool s_brightnessDirty = false;
static uint32_t s_lastBrightnessChangeMs = 0;

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

static void drawBrightnessSettings() {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, kContentTopY + 10, "Brightness");

  const uint8_t percent = static_cast<uint8_t>((static_cast<uint16_t>(g_brightness) * 100) / 255);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%u%%", percent);
  const uint8_t pw = display.getStrWidth(pctBuf);
  display.drawStr(kScreenWidth - kRightMargin - pw, kContentTopY + 10, pctBuf);

  constexpr uint8_t kBarX = 4;
  const uint8_t barY = kContentTopY + 18;
  constexpr uint8_t kBarWidth = kScreenWidth - 2 * kBarX;
  constexpr uint8_t kBarHeight = 16;
  display.drawFrame(kBarX, barY, kBarWidth, kBarHeight);
  const uint8_t fillW = static_cast<uint8_t>((static_cast<uint32_t>(g_brightness) * (kBarWidth - 2)) / 255);
  if (fillW > 0) {
    display.drawBox(kBarX + 1, barY + 1, fillW, kBarHeight - 2);
  }
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

  // PageConfigStore::begin() has already run by this point (called earlier
  // in main.cpp's setup(), before DisplayManager::begin()) -- reload here
  // because the page objects themselves were constructed during global
  // static init, before LittleFS was even mounted, so their constructors
  // couldn't read real config yet.
  s_page2.reloadConfig();
  s_page3.reloadConfig();
  s_page4.reloadConfig();
  s_page5.reloadConfig();

  RotaryEncoder::begin();

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

  // Encoder input -- skipped while the WiFi setup portal is showing, so
  // rotation/button presses don't interfere with that flow.
  RotaryEncoder::loop();
  if (!WifiManager::isPortalActive()) {
    if (RotaryEncoder::consumeButtonPress()) {
      if (s_settingsMode) {
        s_settingsMode = false;
        if (s_brightnessDirty) {
          // Flush immediately on exit even if the idle debounce window
          // below hasn't elapsed yet -- a change should never be lost.
          HostMonitor::saveBrightness(g_brightness);
          s_brightnessDirty = false;
        }
        s_currentPageIndex = 0;  // always returns to Network specifically
        kPages[s_currentPageIndex]->onSelect();
        Logger::log("DisplayManager: exited settings mode, returned to Network page");
      } else {
        s_settingsMode = true;
        Logger::log("DisplayManager: entered settings mode (brightness)");
      }
    }

    const int rotationDelta = RotaryEncoder::consumeRotationDelta();
    if (rotationDelta != 0) {
      if (s_settingsMode) {
        int newBrightness = static_cast<int>(g_brightness) + rotationDelta * kBrightnessStepPerDetent;
        newBrightness = constrain(newBrightness, 0, 255);
        g_brightness = static_cast<uint8_t>(newBrightness);
        applyBrightness(g_brightness);
        s_brightnessDirty = true;
        s_lastBrightnessChangeMs = now;
      } else {
        for (int i = 0; i < abs(rotationDelta); ++i) {
          if (rotationDelta > 0) {
            nextPage();
          } else {
            previousPage();
          }
        }
      }
    }
  }

  // Debounced flash save -- see the comment on kBrightnessSaveDebounceMs
  // above for why this doesn't just save on every detent.
  if (s_brightnessDirty && (now - s_lastBrightnessChangeMs >= kBrightnessSaveDebounceMs)) {
    HostMonitor::saveBrightness(g_brightness);
    s_brightnessDirty = false;
  }

  display.clearBuffer();

  if (WifiManager::isPortalActive()) {
    drawHeader("Setup", false);
    drawPortalPrompt();
  } else if (s_settingsMode) {
    drawHeader("Settings", false);
    drawBrightnessSettings();
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

void DisplayManager::reloadPageConfig(uint8_t pageNumber) {
  MetricsPage *page = nullptr;
  switch (pageNumber) {
    case 2: page = &s_page2; break;
    case 3: page = &s_page3; break;
    case 4: page = &s_page4; break;
    case 5: page = &s_page5; break;
    default:
      Logger::log("DisplayManager: reloadPageConfig ignored out-of-range page " + String(pageNumber));
      return;
  }
  page->reloadConfig();
  Logger::log("DisplayManager: page " + String(pageNumber) + " config reloaded ('" +
               page->name() + "')");
}
