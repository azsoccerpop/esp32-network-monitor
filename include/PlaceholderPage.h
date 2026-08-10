#pragma once
#include "Page.h"

// Generic "coming soon" page -- used for pages that are reserved in the page
// order but not implemented yet. Shows its name plus a placeholder message
// in the content area, so cycling through pages (once the encoder exists)
// shows a real screen for each rather than nothing/garbage.
//
// The name is user-configurable via the web UI (persisted in settings.json)
// and mutable at runtime, so a rename takes effect immediately without a
// reboot -- see DisplayManager::setPageName().
class PlaceholderPage : public Page {
public:
  explicit PlaceholderPage(const String &pageName) : pageName_(pageName) {}

  const char *name() const override { return pageName_.c_str(); }
  void draw(U8G2 &display) override;

  void setName(const String &name) { pageName_ = name; }

private:
  String pageName_;
};
