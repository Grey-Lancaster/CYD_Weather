#pragma once

#include "display/DisplayManager.h"
#include "network/NetworkManager.h"
#include "network/TimeManager.h"
#include "weather/WeatherAlertManager.h"
#include "weather/WeatherService.h"

// Persistent top strip: location on the left, best-effort date centred,
// time + a coloured network-state dot on the right. Self-dirty-checking:
// repaints only when
// the composed text changes (or when forced by a full redraw) — the time
// naturally ticks over to a new minute-string once a minute, which is what
// drives its own redraw cadence, no separate timer needed.
//
// Once TimeManager has completed its first NTP sync, date/time come from the
// device's real (DST-aware, via AppSettings::timezone) clock. Until then —
// briefly at boot, or if NTP is unreachable — it falls back to estimating
// from the last successful weather fetch's HH:MM plus elapsed millis() since,
// the same best-effort approach pages/PageSunMoon.h and
// weather/WeatherAlertManager.h still use (they don't consult TimeManager).
//
// Also surfaces weather alerts (see weather/WeatherAlertManager.h): whenever
// one is active, a small severity glyph (see ui/AlertTheme.h) is drawn just
// before the location text — the bar's usual location/date/time content
// stays visible rather than being replaced by a full-width banner. This is
// deliberately done here — shared chrome rendered by every page via
// PageManager, not any individual page's own code — so the glyph appears
// "on every page" without touching any of them (they're locked). Full alert
// detail lives on pages/PageAlerts.cpp, already in the normal page rotation.
class StatusBar {
 public:
  StatusBar(DisplayManager& display, NetworkManager& network, WeatherAlertManager& alerts,
            WeatherService& weather, TimeManager& time);
  void render(bool force);

 private:
  DisplayManager& _display;
  NetworkManager& _network;
  WeatherAlertManager& _alerts;
  WeatherService& _weather;
  TimeManager& _time;
  char _lastText[112] = {0};  // matches the "combined" buffer size in render()

  // Best-effort clock anchor (see class comment above).
  uint32_t _lastSeenWeatherVersion = 0xFFFFFFFFu;
  int16_t _fetchMinutesOfDay = -1;  // -1 = never fetched yet
  uint32_t _fetchMillis = 0;
  char _cachedDate[16] = {0};      // e.g. "Sat, Aug 1" — from the last fetch's daily[0]
  char _cachedLocation[40] = {0};  // AppSettings::locationName as of the last fetch
};
