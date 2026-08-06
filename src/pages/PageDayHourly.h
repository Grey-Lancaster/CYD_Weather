#pragma once

#include <functional>

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "weather/WeatherService.h"

// Reached only by tapping a day tile on the 5 Day Forecast page (see
// pages/ForecastPage.h's onDayTapped callback, wired in main.cpp) — not
// part of the normal page rotation (registered with rotatable=false, same
// as pages/SetupPage.h, so it never shows a page-indicator dot and
// auto-rotation can't land on or leave it on a timer).
//
// Shows every fetched hour of one specific day, sliced out of
// WeatherData::hourly by scanning for "00:00" boundaries — see
// hourRangeForDay() in the .cpp. The hourly block now covers all 5 forecast
// days in the one shared Open-Meteo request (see
// cfg::weather::HOURLY_FETCH_COUNT), so this needs no fetch of its own.
//
// Any tap anywhere on this page returns to the 5 Day Forecast page — with
// no page-indicator dots and no obvious swipe target here, "tap to go back"
// is the simplest, most discoverable gesture (a hint label says so too).
class PageDayHourly : public Page {
 public:
  using BackCallback = std::function<void()>;

  PageDayHourly(DisplayManager& display, WeatherService& weather);

  const char* name() const override { return "day_hourly"; }
  void onEnter() override;
  void render(bool full) override;
  void tick(uint32_t nowMs) override;
  bool handleTap(int16_t x, int16_t y) override;

  // Which of the 5 forecast days (0 = today .. 4) to show — set by
  // ForecastPage's onDayTapped callback before switching to this page.
  void setDayIndex(uint8_t dayIndex) { _dayIndex = dayIndex; }
  void onBack(BackCallback cb) { _onBack = std::move(cb); }

 private:
  DisplayManager& _display;
  WeatherService& _weather;
  BackCallback _onBack;
  uint8_t _dayIndex = 0;
  uint32_t _lastSeenVersion = 0;
  bool _lastSeenFailed = false;
  // Same "shift the visible window forward" idea as
  // pages/HourlyForecastPage.cpp, since a full day can have more hours than
  // fit on screen at once.
  uint8_t _windowStart = 0;
  uint32_t _lastShiftMs = 0;
};
