#pragma once
#include <Arduino.h>

// One of up to 4 configurable data points on a page. `query` is a full
// InfluxQL query (can select multiple aliased fields in one query, e.g. to
// avoid hitting Influx 3x for 3 related values); `field` names which column
// to use from the result -- leave empty to use the first non-"time" column.
struct MetricWidget {
  String label;
  String query;
  String field;
};

enum class PageFormat { Table, Barchart };

struct PageConfig {
  String name;
  PageFormat format = PageFormat::Table;
  MetricWidget widgets[4];
};

// Persists per-page (2-5) configuration to /page_config.json. Deliberately
// NOT seeded from the data/ folder -- see the comment in HostMonitor.cpp's
// loadHosts() for why shipping a seed file there causes it to be wiped on
// every filesystem image upload. This file is created fresh by the device
// the first time a page is configured.
class PageConfigStore {
public:
  static void begin();

  // pageNumber must be 2-5.
  static PageConfig getPageConfig(uint8_t pageNumber);
  static void savePageMeta(uint8_t pageNumber, const String &name, PageFormat format);
  static void saveWidgets(uint8_t pageNumber, const MetricWidget widgets[4]);

  static const char *formatToString(PageFormat f);
  static PageFormat formatFromString(const String &s);
};
