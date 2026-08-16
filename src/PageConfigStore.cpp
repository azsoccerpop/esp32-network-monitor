#include "PageConfigStore.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
constexpr uint8_t kNumConfigurablePages = 4;  // pages 2-5
PageConfig s_pages[kNumConfigurablePages];    // index 0 = page 2, ... index 3 = page 5

int8_t indexForPage(uint8_t pageNumber) {
  if (pageNumber < 2 || pageNumber > 5) return -1;
  return static_cast<int8_t>(pageNumber - 2);
}

void save() {
  File f = LittleFS.open("/page_config.json", "w");
  if (!f) return;

  DynamicJsonDocument doc(4096);
  for (uint8_t i = 0; i < kNumConfigurablePages; ++i) {
    JsonObject p = doc.createNestedObject(String(i + 2));
    p["name"] = s_pages[i].name;
    p["format"] = PageConfigStore::formatToString(s_pages[i].format);
    JsonArray widgets = p.createNestedArray("widgets");
    for (const auto &w : s_pages[i].widgets) {
      JsonObject wo = widgets.createNestedObject();
      wo["label"] = w.label;
      wo["query"] = w.query;
      wo["field"] = w.field;
      wo["max"] = w.maxValue;
    }
  }
  serializeJson(doc, f);
  f.close();
}

void load() {
  // Sensible in-code defaults if the file doesn't exist yet (fresh device,
  // or first boot after this feature shipped) -- matches the names already
  // shown on the OLED/web UI before any customization. Page 2 additionally
  // ships with the Disk Usage example from HomeNAS1 pre-filled across 3
  // widget slots (all sharing one query, since MetricsManager dedupes
  // identical query strings within a poll cycle) -- edit host/volume to
  // match your actual setup, or clear it out and start fresh.
  const char *defaultNames[kNumConfigurablePages] = {"Disk Usage", "Page 3", "Page 4", "Page 5"};
  for (uint8_t i = 0; i < kNumConfigurablePages; ++i) {
    s_pages[i].name = defaultNames[i];
    s_pages[i].format = PageFormat::Table;
  }

  static const char *kDiskUsageQuery =
      "SELECT last(\"raidTotalSize\") - last(\"raidFreeSize\") AS used_bytes, "
      "last(\"raidTotalSize\") AS total_bytes, "
      "(last(\"raidTotalSize\") - last(\"raidFreeSize\")) / last(\"raidTotalSize\") * 100 AS used_pct "
      "FROM \"synology_volume\" WHERE (\"host\" = 'homenas1' AND \"raidName\" = 'Volume 1')";
  s_pages[0].widgets[0] = {"Used", kDiskUsageQuery, "used_bytes", ""};
  s_pages[0].widgets[1] = {"Total", kDiskUsageQuery, "total_bytes", ""};
  s_pages[0].widgets[2] = {"Used %", kDiskUsageQuery, "used_pct", "100"};

  if (!LittleFS.exists("/page_config.json")) return;
  File f = LittleFS.open("/page_config.json", "r");
  if (!f) return;

  size_t size = f.size();
  std::unique_ptr<char[]> buf(new char[size + 1]);
  f.readBytes(buf.get(), size);
  buf[size] = '\0';
  f.close();

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, buf.get())) {
    Serial.println("PageConfigStore: failed to parse page_config.json, using defaults");
    return;
  }

  for (uint8_t i = 0; i < kNumConfigurablePages; ++i) {
    JsonObject p = doc[String(i + 2)];
    if (p.isNull()) continue;
    s_pages[i].name = p["name"] | s_pages[i].name;
    s_pages[i].format = PageConfigStore::formatFromString(p["format"] | "table");

    JsonArray widgets = p["widgets"];
    uint8_t slot = 0;
    for (JsonObject wo : widgets) {
      if (slot >= 4) break;
      s_pages[i].widgets[slot].label = wo["label"] | "";
      s_pages[i].widgets[slot].query = wo["query"] | "";
      s_pages[i].widgets[slot].field = wo["field"] | "";
      s_pages[i].widgets[slot].maxValue = wo["max"] | "";
      ++slot;
    }
  }
}
}  // namespace

void PageConfigStore::begin() {
  load();
}

PageConfig PageConfigStore::getPageConfig(uint8_t pageNumber) {
  const int8_t idx = indexForPage(pageNumber);
  if (idx < 0) return PageConfig{};
  return s_pages[idx];
}

void PageConfigStore::savePageMeta(uint8_t pageNumber, const String &name, PageFormat format) {
  const int8_t idx = indexForPage(pageNumber);
  if (idx < 0) return;
  s_pages[idx].name = name;
  s_pages[idx].format = format;
  save();
}

void PageConfigStore::saveWidgets(uint8_t pageNumber, const MetricWidget widgets[4]) {
  const int8_t idx = indexForPage(pageNumber);
  if (idx < 0) return;
  for (uint8_t i = 0; i < 4; ++i) {
    s_pages[idx].widgets[i] = widgets[i];
  }
  save();
}

const char *PageConfigStore::formatToString(PageFormat f) {
  return f == PageFormat::Barchart ? "barchart" : "table";
}

PageFormat PageConfigStore::formatFromString(const String &s) {
  return s == "barchart" ? PageFormat::Barchart : PageFormat::Table;
}
