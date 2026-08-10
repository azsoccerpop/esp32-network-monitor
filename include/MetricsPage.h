#pragma once
#include "Page.h"
#include "PageConfigStore.h"

// Renders up to 4 InfluxDB-backed data points (configured via the web UI)
// as either a Table or a Barchart. Falls back to "Not configured" if no
// widgets have both a label and a query set yet.
class MetricsPage : public Page {
public:
  explicit MetricsPage(uint8_t pageNumber);

  const char *name() const override { return name_.c_str(); }
  void draw(U8G2 &display) override;

  // Re-reads name/format/widgets from PageConfigStore. Call after a web UI
  // save so the change takes effect immediately without a reboot.
  void reloadConfig();

private:
  uint8_t pageNumber_;
  String name_;
  PageFormat format_ = PageFormat::Table;

  void drawTable(U8G2 &display);
  void drawBarchart(U8G2 &display);
};
