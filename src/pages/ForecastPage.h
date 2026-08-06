#pragma once

#include <functional>

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "weather/WeatherService.h"

// Page 3 — the default boot page: five rounded forecast cards (today +
// next 4 days), sourced live from WeatherService (Open-Meteo). Redraws only
// when a new fetch lands (or the offline status changes) — see tick().
//
// Tapping a card (see handleTap()) drills into that day's hourly breakdown
// — see pages/PageDayHourly.h — via a callback rather than a direct
// PageManager/PageDayHourly reference, the same way every other cross-page
// navigation trigger in this app is wired (see main.cpp's
// NetworkManager::onStateChange for the established pattern); this page
// doesn't need to know PageDayHourly or PageManager exist.
class ForecastPage : public Page {
 public:
  using DayTapCallback = std::function<void(uint8_t dayIndex)>;

  ForecastPage(DisplayManager& display, WeatherService& weather);

  const char* name() const override { return "forecast"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;
  bool handleTap(int16_t x, int16_t y) override;

  // `cb` is called with the tapped day's index (0 = today .. 4) whenever a
  // tap lands on a valid, populated forecast card.
  void onDayTapped(DayTapCallback cb) { _onDayTapped = std::move(cb); }

 private:
  DisplayManager& _display;
  WeatherService& _weather;
  uint32_t _lastSeenVersion = 0;
  bool _lastSeenFailed = false;
  DayTapCallback _onDayTapped;
};
