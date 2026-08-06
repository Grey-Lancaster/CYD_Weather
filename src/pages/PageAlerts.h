#pragma once

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "weather/WeatherAlertManager.h"
#include "weather/WeatherService.h"

// Page 6 — lists every currently active weather alert (see
// weather/WeatherAlertManager.h): message, reason, time issued, expected
// duration and severity. Shows "No active weather alerts" when the list is
// empty. This page only ever *reads* WeatherAlertManager's already-evaluated
// list — it does no evaluation itself (see main.cpp's "alerts.eval"
// scheduler task, which calls WeatherAlertManager::evaluate() whenever new
// weather data lands).
class PageAlerts : public Page {
 public:
  PageAlerts(DisplayManager& display, WeatherAlertManager& alerts, WeatherService& weather);

  const char* name() const override { return "alerts"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;

 private:
  DisplayManager& _display;
  WeatherAlertManager& _alerts;
  WeatherService& _weather;

  uint32_t _lastSeenAlertVersion = 0xFFFFFFFFu;  // mismatches WeatherAlertManager's initial 0 -> first tick redraws
  bool _lastSeenFailed = false;
  // Only kVisibleCards (see .cpp) alerts fit on screen at once; if more are
  // active, the visible window auto-advances every few seconds, same
  // "shift through the rest" idea as pages/HourlyForecastPage.cpp.
  uint8_t _windowStart = 0;
  uint32_t _lastAdvanceMs = 0;
};
