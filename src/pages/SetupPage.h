#pragma once

#include "display/DisplayManager.h"
#include "network/NetworkManager.h"
#include "pages/Page.h"

// First-run / recovery page: shows how to reach the captive portal (AP name,
// password, portal URL) plus a live connection status card. main.cpp switches
// here automatically whenever the network enters AP mode, and back to home
// once connected. Also reachable in the normal page cycle.
class SetupPage : public Page {
 public:
  SetupPage(DisplayManager& display, NetworkManager& network);

  const char* name() const override { return "setup"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;

 private:
  void renderApCard();
  void renderStatusCard();

  DisplayManager& _display;
  NetworkManager& _network;
  uint32_t _lastRefreshMs = 0;
};
