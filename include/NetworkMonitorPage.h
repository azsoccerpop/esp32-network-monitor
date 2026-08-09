#pragma once
#include "Page.h"

// The original (and so far only real) page: cycles through configured hosts
// showing UP/DOWN + latency, blinking DOWN hosts, paging through the list a
// screenful at a time if there are more hosts than fit.
class NetworkMonitorPage : public Page {
public:
  const char *name() const override { return "Network"; }
  void draw(U8G2 &display) override;
  bool showIpInHeader() const override { return true; }
};
