#pragma once

#include <functional>

#include "display/DisplayManager.h"
#include "network/NetworkManager.h"
#include "pages/Page.h"

// Shown for cfg::ui::CONFIG_INFO_S seconds right after the device first joins
// WiFi: tells the user where to open the settings portal (IP address and
// hostname) to set location, units and so on. A tap skips it. Afterwards the
// same information stays available on System Information (BOOT button).
class PageConfigInfo : public Page {
 public:
  PageConfigInfo(DisplayManager& display, NetworkManager& network);

  const char* name() const override { return "configinfo"; }
  void onEnter() override;
  void render(bool full) override;
  bool handleTap(int16_t x, int16_t y) override;
  void tick(uint32_t nowMs) override;

  // Called once when the countdown ends or the screen is tapped.
  void onDone(std::function<void()> cb) { _onDone = cb; }

 private:
  void finish();

  DisplayManager& _display;
  NetworkManager& _network;
  std::function<void()> _onDone;
  uint32_t _enteredMs = 0;
  int32_t _secondsLeft = 0;
  bool _finished = false;
};
