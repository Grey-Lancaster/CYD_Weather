#pragma once

#include "display/DisplayManager.h"
#include "network/NetworkManager.h"
#include "pages/Page.h"

// Page 5 — firmware version, IP address, WiFi signal, free heap and uptime.
// The live fields (heap/uptime/signal) refresh once a second; only the value
// column repaints on those ticks, so the card border/labels are drawn once.
class SystemPage : public Page {
 public:
  SystemPage(DisplayManager& display, NetworkManager& network);

  const char* name() const override { return "system"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;

 private:
  DisplayManager& _display;
  NetworkManager& _network;
  uint32_t _lastRefreshMs = 0;
};
