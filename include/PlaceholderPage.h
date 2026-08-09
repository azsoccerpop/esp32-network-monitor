#pragma once
#include "Page.h"

// Generic "coming soon" page -- used for pages that are reserved in the page
// order but not implemented yet. Shows its name plus a placeholder message
// in the content area, so cycling through pages (once the encoder exists)
// shows a real screen for each rather than nothing/garbage.
class PlaceholderPage : public Page {
public:
  explicit PlaceholderPage(const char *pageName) : pageName_(pageName) {}

  const char *name() const override { return pageName_; }
  void draw(U8G2 &display) override;

private:
  const char *pageName_;
};
