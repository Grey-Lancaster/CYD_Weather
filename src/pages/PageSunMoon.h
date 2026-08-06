#pragma once

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "weather/WeatherService.h"

// Page 5 — sunrise/solar noon/sunset/day length (from Open-Meteo's daily
// block, the same shared fetch every other page already uses — see
// weather/WeatherService.cpp's kDailyParams) plus a locally-computed moon
// phase, illumination, age and best-effort moonrise/moonset. Open-Meteo has
// no moon data at all, so the moon side is computed on-device — see this
// page's astro:: namespace (PageSunMoon.cpp) for the low-precision lunar
// position algorithm, deliberately kept free of any TFT_eSPI dependency.
//
// No on-device clock exists (see weather/WeatherData.h) — "now" is
// estimated from the last fetch's HH:MM plus elapsed millis() since, same
// best-effort approach every other live page already uses. tick() advances
// that estimate and requests a redraw once the whole-minute value changes,
// satisfying "recalculate daylight progress every minute" without polling
// any faster than the other pages do.
class PageSunMoon : public Page {
 public:
  PageSunMoon(DisplayManager& display, WeatherService& weather);

  const char* name() const override { return "sunmoon"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;

 private:
  DisplayManager& _display;
  WeatherService& _weather;
  uint32_t _lastSeenVersion = 0;
  bool _lastSeenFailed = false;

  // Best-effort "now" clock (see class comment above).
  int16_t _fetchMinutesOfDay = -1;  // WeatherData::lastUpdate at fetch time; -1 = never fetched
  uint32_t _fetchMillis = 0;
  int16_t _lastDrawnMinute = -1;    // minutes-since-midnight last used to render; drives tick()'s
                                    // once-a-minute redraw and is read straight back by render()
};
