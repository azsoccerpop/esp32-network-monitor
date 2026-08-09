#pragma once
#include <U8g2lib.h>

// A single "screen" of content on the OLED, selectable via the (eventually
// encoder-driven) page index in DisplayManager. DisplayManager owns drawing
// the shared header/chrome (title + IP + rule line) before handing off to
// the active page -- implementations should only draw within the content
// area below DisplayGeometry::kContentTopY.
class Page {
public:
  virtual ~Page() = default;

  // Short name shown in the header to indicate what's currently on screen,
  // e.g. "Network", "Drive Usage". Keep this short -- the header also has
  // to fit the IP address on the same line.
  virtual const char *name() const = 0;

  // Called once when the page becomes active (e.g. on page switch). Optional
  // -- override to reset any per-page animation/paging state.
  virtual void onSelect() {}

  // Called every DisplayManager::loop() iteration while this page is
  // active. Draw only into the content area; DisplayManager has already
  // cleared the buffer and drawn the header before calling this, and will
  // call display.sendBuffer() after.
  virtual void draw(U8G2 &display) = 0;
};
