#include "Logger.h"
#include <esp_system.h>
#include <WiFi.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

// Ring buffer capacity. Each entry is a short-ish string, so ~80 entries
// keeps memory use modest (a few KB) while giving enough history to catch
// what happened before a freeze/reboot.
constexpr size_t kMaxEntries = 80;

std::vector<String> s_entries;
size_t s_nextIndex = 0;
bool s_full = false;
SemaphoreHandle_t s_mutex = nullptr;

const char *resetReasonToStr(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software reset";
    case ESP_RST_PANIC: return "panic/exception";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
  }
}

}  // namespace

void Logger::begin() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
  s_entries.assign(kMaxEntries, String());
  s_nextIndex = 0;
  s_full = false;
}

void Logger::log(const String &line) {
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "[%lu] ", static_cast<unsigned long>(millis()));
  String full = String(prefix) + line;

  Serial.println(full);

  if (s_mutex == nullptr) return;  // begin() not called yet
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    s_entries[s_nextIndex] = full;
    s_nextIndex = (s_nextIndex + 1) % kMaxEntries;
    if (s_nextIndex == 0) s_full = true;
    xSemaphoreGive(s_mutex);
  }
}

void Logger::logBootInfo() {
  esp_reset_reason_t reason = esp_reset_reason();
  log(String("Boot. Reset reason: ") + resetReasonToStr(reason) +
      ", free heap: " + String(ESP.getFreeHeap()) + " bytes");
}

String Logger::getLogsJson() {
  String out = "[";
  if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    const size_t count = s_full ? kMaxEntries : s_nextIndex;
    const size_t startIdx = s_full ? s_nextIndex : 0;
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
      const size_t idx = (startIdx + i) % kMaxEntries;
      const String &entry = s_entries[idx];
      if (!first) out += ",";
      first = false;
      out += "\"";
      // Minimal JSON-string escaping: backslash, quote, and control chars.
      for (size_t c = 0; c < entry.length(); ++c) {
        char ch = entry[c];
        if (ch == '"' || ch == '\\') {
          out += '\\';
          out += ch;
        } else if (ch == '\n' || ch == '\r') {
          out += ' ';
        } else {
          out += ch;
        }
      }
      out += "\"";
    }
    xSemaphoreGive(s_mutex);
  }
  out += "]";
  return out;
}

void Logger::clear() {
  if (s_mutex != nullptr && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    s_entries.assign(kMaxEntries, String());
    s_nextIndex = 0;
    s_full = false;
    xSemaphoreGive(s_mutex);
  }
}
