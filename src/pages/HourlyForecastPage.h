#pragma once

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "weather/WeatherService.h"

// Page 3 — a timeline of the next 12 hours, sourced live from WeatherService
// (Open-Meteo): rounded gradient cards in the same visual language as
// ForecastPage's 5-day cards, but only kVisibleCards are shown at once — the
// visible window automatically shifts forward through all 12 hours (see
// tick()) since they don't all fit on screen at a readable size. Redraws
// when a new fetch lands, the offline status changes, or the window shifts.
class HourlyForecastPage : public Page {
 public:
  HourlyForecastPage(DisplayManager& display, WeatherService& weather);

  const char* name() const override { return "hourly"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;

 private:
  DisplayManager& _display;
  WeatherService& _weather;
  uint32_t _lastSeenVersion = 0;
  bool _lastSeenFailed = false;
  uint8_t _hourlyCount = 0;  // cached from the last successful fetch (see tick())
  uint8_t _windowStart = 0;  // first visible hour's index into WeatherData::hourly[]
  uint32_t _lastShiftMs = 0;
};
