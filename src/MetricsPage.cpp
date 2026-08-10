#include "MetricsPage.h"
#include "MetricsManager.h"
#include "DisplayGeometry.h"

using namespace DisplayGeometry;

MetricsPage::MetricsPage(uint8_t pageNumber) : pageNumber_(pageNumber) {
  // Deliberately NOT calling reloadConfig() here -- this constructor runs
  // during global static initialization, before PageConfigStore::begin()
  // has loaded anything from LittleFS (the filesystem isn't even mounted
  // yet at that point). DisplayManager::begin() calls reloadConfig() on
  // every page after PageConfigStore::begin() has actually run.
  name_ = "Page";
}

void MetricsPage::reloadConfig() {
  const PageConfig cfg = PageConfigStore::getPageConfig(pageNumber_);
  name_ = cfg.name;
  format_ = cfg.format;
}

namespace {
bool hasAnyConfiguredWidget(const PageConfig &cfg) {
  for (const auto &w : cfg.widgets) {
    if (w.label.length() > 0 && w.query.length() > 0) return true;
  }
  return false;
}
}  // namespace

void MetricsPage::draw(U8G2 &display) {
  const PageConfig cfg = PageConfigStore::getPageConfig(pageNumber_);

  if (!hasAnyConfiguredWidget(cfg)) {
    display.setFont(u8g2_font_helvR08_tr);
    display.drawStr(0, kContentTopY + 12, "Not configured");
    return;
  }

  if (format_ == PageFormat::Barchart) {
    drawBarchart(display);
  } else {
    drawTable(display);
  }
}

void MetricsPage::drawTable(U8G2 &display) {
  const PageConfig cfg = PageConfigStore::getPageConfig(pageNumber_);
  display.setFont(u8g2_font_helvR08_tr);

  constexpr uint8_t kRowHeight = 10;
  for (uint8_t i = 0; i < 4; ++i) {
    const auto &w = cfg.widgets[i];
    if (w.label.length() == 0) continue;
    const uint8_t y = kContentTopY + i * kRowHeight;

    display.drawStr(0, y, w.label.c_str());

    const MetricValue mv = MetricsManager::getValue(pageNumber_, i);
    String valueStr;
    if (mv.lastUpdatedMs == 0) {
      valueStr = "...";  // never successfully polled yet
    } else if (!mv.ok) {
      valueStr = "err";
    } else {
      valueStr = mv.displayValue;
    }
    const uint8_t vw = display.getStrWidth(valueStr.c_str());
    const uint8_t vx = (vw + kRightMargin < kScreenWidth) ? (kScreenWidth - kRightMargin - vw) : 0;
    display.drawStr(vx, y, valueStr.c_str());
  }
}

void MetricsPage::drawBarchart(U8G2 &display) {
  const PageConfig cfg = PageConfigStore::getPageConfig(pageNumber_);
  display.setFont(u8g2_font_helvR08_tr);

  MetricValue values[4];
  double maxVal = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    values[i] = MetricsManager::getValue(pageNumber_, i);
    if (values[i].ok && values[i].numericValue > maxVal) {
      maxVal = values[i].numericValue;
    }
  }

  constexpr uint8_t kRowHeight = 10;
  constexpr uint8_t kBarX = 30;
  constexpr uint8_t kBarMaxWidth = 70;
  constexpr uint8_t kBarHeight = 7;

  for (uint8_t i = 0; i < 4; ++i) {
    const auto &w = cfg.widgets[i];
    if (w.label.length() == 0) continue;
    const uint8_t y = kContentTopY + i * kRowHeight;

    char labelBuf[8];
    snprintf(labelBuf, sizeof(labelBuf), "%.6s", w.label.c_str());
    display.drawStr(0, y, labelBuf);

    display.drawFrame(kBarX, y - kBarHeight, kBarMaxWidth, kBarHeight);
    if (values[i].ok && maxVal > 0) {
      const uint8_t fillW =
          static_cast<uint8_t>((values[i].numericValue / maxVal) * (kBarMaxWidth - 2));
      if (fillW > 0) {
        display.drawBox(kBarX + 1, y - kBarHeight + 1, fillW, kBarHeight - 2);
      }
    }
  }
}
