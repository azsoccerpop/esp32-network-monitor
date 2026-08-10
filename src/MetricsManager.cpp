#include "MetricsManager.h"
#include "PageConfigStore.h"
#include "HostMonitor.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <map>
#include <vector>

namespace {

constexpr uint32_t kPollIntervalMs = 60UL * 1000UL;
constexpr uint8_t kNumPages = 4;   // pages 2-5
constexpr uint8_t kNumSlots = 4;   // widgets per page

MetricValue s_cache[kNumPages][kNumSlots];
SemaphoreHandle_t s_mutex = nullptr;

int8_t indexForPage(uint8_t pageNumber) {
  if (pageNumber < 2 || pageNumber > 5) return -1;
  return static_cast<int8_t>(pageNumber - 2);
}

String urlEncode(const String &s) {
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
      out += buf;
    }
  }
  return out;
}

// Result of running one InfluxQL query, with just enough extracted to look
// up any field by name -- deliberately not tied to ArduinoJson types so it
// can be cached cheaply per unique query string within a poll cycle.
struct RawQueryResult {
  bool ok = false;
  String error;
  std::vector<String> columns;
  std::vector<String> firstRowValues;
};

RawQueryResult runQuery(const String &baseUrl, const String &database, const String &username,
                         const String &password, const String &query) {
  RawQueryResult result;

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  String url = baseUrl + "/query?q=" + urlEncode(query);
  if (database.length() > 0) {
    url += "&db=" + urlEncode(database);
  }
  http.begin(url);
  if (username.length() > 0) {
    http.setAuthorization(username.c_str(), password.c_str());
  }

  const int code = http.GET();
  if (code != 200) {
    result.error = "HTTP " + String(code);
    http.end();
    return result;
  }

  const String body = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) {
    result.error = "parse error";
    return result;
  }

  JsonObject series = doc["results"][0]["series"][0];
  if (series.isNull()) {
    result.error = "no data";
    return result;
  }

  for (JsonVariant c : series["columns"].as<JsonArray>()) {
    result.columns.push_back(c.as<String>());
  }
  JsonArray values = series["values"][0];
  if (values.isNull()) {
    result.error = "no rows";
    return result;
  }
  for (JsonVariant v : values) {
    result.firstRowValues.push_back(v.as<String>());
  }

  result.ok = true;
  return result;
}

// Picks which column to use for a widget: the named field if given, else
// the first column that isn't "time" (InfluxQL always returns "time"
// first).
int findColumnIndex(const RawQueryResult &r, const String &field) {
  if (field.length() > 0) {
    for (size_t i = 0; i < r.columns.size(); ++i) {
      if (r.columns[i].equalsIgnoreCase(field)) return static_cast<int>(i);
    }
    return -1;
  }
  for (size_t i = 0; i < r.columns.size(); ++i) {
    if (!r.columns[i].equalsIgnoreCase("time")) return static_cast<int>(i);
  }
  return -1;
}

String humanizeBytes(double bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  double v = bytes;
  size_t unit = 0;
  while (v >= 1024.0 && unit < 5) {
    v /= 1024.0;
    ++unit;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f %s", v, units[unit]);
  return String(buf);
}

// Heuristic formatting based on the field name -- good enough for the
// common cases (byte counts, percentages) without needing the user to
// specify a unit/format per widget.
String formatValue(const String &field, double value) {
  String f = field;
  f.toLowerCase();
  if (f.indexOf("byte") >= 0) {
    return humanizeBytes(value);
  }
  if (f.indexOf("pct") >= 0 || f.indexOf("percent") >= 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f%%", value);
    return String(buf);
  }
  if (value == static_cast<int64_t>(value)) {
    return String(static_cast<int64_t>(value));
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f", value);
  return String(buf);
}

void setCache(uint8_t pageIdx, uint8_t slot, const MetricValue &v) {
  if (s_mutex == nullptr) return;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    s_cache[pageIdx][slot] = v;
    xSemaphoreGive(s_mutex);
  }
}

}  // namespace

void MetricsManager::pollAll() {
  const auto s = HostMonitor::getSettings();
  if (s.influx_host.length() == 0) return;  // not configured yet

  const String baseUrl = "http://" + s.influx_host + ":" + String(s.influx_port);

  // Dedupe identical query strings within this cycle -- e.g. the Disk Usage
  // page's 3 widgets sharing one query for used/total/pct only needs one
  // HTTP round-trip, not three.
  std::map<String, RawQueryResult> queryCache;

  for (uint8_t pageIdx = 0; pageIdx < kNumPages; ++pageIdx) {
    const PageConfig cfg = PageConfigStore::getPageConfig(pageIdx + 2);
    for (uint8_t slot = 0; slot < kNumSlots; ++slot) {
      const MetricWidget &w = cfg.widgets[slot];
      if (w.query.length() == 0) continue;

      if (queryCache.find(w.query) == queryCache.end()) {
        queryCache[w.query] = runQuery(baseUrl, s.influx_database, s.influx_username, s.influx_password, w.query);
      }
      const RawQueryResult &r = queryCache[w.query];

      MetricValue mv;
      mv.lastUpdatedMs = millis();
      if (!r.ok) {
        mv.ok = false;
        mv.error = r.error;
      } else {
        const int colIdx = findColumnIndex(r, w.field);
        if (colIdx < 0 || static_cast<size_t>(colIdx) >= r.firstRowValues.size()) {
          mv.ok = false;
          mv.error = "field '" + w.field + "' not found";
        } else {
          mv.ok = true;
          mv.numericValue = r.firstRowValues[colIdx].toDouble();
          mv.displayValue = formatValue(w.field, mv.numericValue);
        }
      }
      setCache(pageIdx, slot, mv);
    }
  }
}

void MetricsManager::taskFn(void *param) {
  (void)param;
  // Small initial delay so this doesn't compete with WiFi/services still
  // coming up right at boot.
  vTaskDelay(pdMS_TO_TICKS(5000));
  for (;;) {
    pollAll();
    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}

void MetricsManager::begin() {
  s_mutex = xSemaphoreCreateMutex();
  // Pinned to core 0 (same as WiFi/AsyncTCP internals), separate from the
  // main loop() on core 1 that DisplayManager's I2C timing depends on --
  // a slow or hung Influx server can only affect this task, never the
  // display's responsiveness.
  xTaskCreatePinnedToCore(taskFn, "MetricsPoll", 8192, nullptr, 1, nullptr, 0);
  Logger::log("MetricsManager: background polling task started");
}

MetricValue MetricsManager::getValue(uint8_t pageNumber, uint8_t slot) {
  const int8_t idx = indexForPage(pageNumber);
  MetricValue out;
  if (idx < 0 || slot >= kNumSlots || s_mutex == nullptr) return out;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    out = s_cache[idx][slot];
    xSemaphoreGive(s_mutex);
  }
  return out;
}

void MetricsManager::refreshNow() {
  pollAll();
}
