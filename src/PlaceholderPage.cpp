#include "PlaceholderPage.h"
#include "DisplayGeometry.h"

using namespace DisplayGeometry;

void PlaceholderPage::draw(U8G2 &display) {
  display.setFont(u8g2_font_helvR08_tr);
  display.drawStr(0, kContentTopY + 12, "Coming soon");
}
