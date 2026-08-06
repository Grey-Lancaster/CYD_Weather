#pragma once

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "weather/WeatherService.h"

// Page 2 — a fuller "commercial weather station" readout than the old
// current-weather card: a huge hero temperature plus an 8-stat grid (feels
// like, humidity, pressure, wind speed/direction, cloud cover, visibility,
// UV index), sourced live from WeatherService (Open-Meteo, one shared
// request — see weather/README.md). Redraws only when a new fetch lands (or
// the offline/no-data status changes) — see tick().
class CurrentConditionsPage : public Page {
 public:
  CurrentConditionsPage(DisplayManager& display, WeatherService& weather);

  const char* name() const override { return "conditions"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;

 private:
  DisplayManager& _display;
  WeatherService& _weather;
  uint32_t _lastSeenVersion = 0;
  bool _lastSeenFailed = false;
};
