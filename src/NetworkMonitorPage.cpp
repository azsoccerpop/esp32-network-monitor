#include "NetworkMonitorPage.h"
#include "DisplayGeometry.h"
#include "HostMonitor.h"

using namespace DisplayGeometry;

namespace {
constexpr uint8_t kHostLineHeight = 12;
constexpr uint8_t kMaxVisibleHostLines = (kScreenHeight - kContentTopY) / kHostLineHeight + 1;
constexpr uint32_t kHostGroupIntervalMs = 3000;  // how long each screenful of hosts is shown
constexpr uint32_t kBlinkIntervalMs = 250;

constexpr uint8_t kNameX = 0;
constexpr uint8_t kStatusX = 62;
constexpr uint8_t kNameMaxChars = 8;

void drawEmptyHostsMessage(U8G2 &display) {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(kNameX, kContentTopY, "No hosts configured");
  display.drawStr(kNameX, kContentTopY + kHostLineHeight, "Add via web UI");
}

void drawHostRow(U8G2 &display, uint8_t y, const HostEntry &h, bool blinkOn) {
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
}  // namespace

void NetworkMonitorPage::draw(U8G2 &display) {
  if (HostMonitor::getHosts().empty()) {
    drawEmptyHostsMessage(display);
    return;
  }

  const auto &hosts = HostMonitor::getHosts();
  const size_t numHosts = hosts.size();
  const size_t numHostGroups = (numHosts + kMaxVisibleHostLines - 1) / kMaxVisibleHostLines;
  const size_t group = (numHostGroups > 1) ? (millis() / kHostGroupIntervalMs) % numHostGroups : 0;
  const size_t startIdx = group * kMaxVisibleHostLines;
  const size_t endIdx = min(startIdx + kMaxVisibleHostLines, numHosts);
  const bool blinkOn = ((millis() / kBlinkIntervalMs) % 2) == 0;

  uint8_t row = 0;
  for (size_t i = startIdx; i < endIdx; ++i, ++row) {
    drawHostRow(display, kContentTopY + row * kHostLineHeight, hosts[i], blinkOn);
  }
}
