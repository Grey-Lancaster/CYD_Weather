#pragma once

#include <Arduino.h>

#include "config/AppConfig.h"

// One hour of forecast data (see weather/WeatherService.h's `hourly=` fetch).
struct HourlyForecastEntry {
  char time[6] = {0};  // "HH:MM", 24-hour
  float temperatureC = 0.0f;
  int16_t weatherCode = -1;  // Open-Meteo WMO code; see weather/WeatherIcons.h
  int8_t rainProbabilityPct = 0;
  float windSpeedKmh = 0.0f;
  bool isDay = true;  // drives day- vs night-variant icons (see ui/WeatherIconTheme.h)
};

// One day of forecast data (see weather/WeatherService.h's `daily=` fetch).
// `dayLabel`/`dateLabel` are computed once at parse time (from the date
// Open-Meteo returns) rather than left as raw ISO text, so pages only ever
// read display-ready strings — see weather/WeatherService.cpp's dayOfWeek().
struct DailyForecastEntry {
  char dayLabel[4] = {0};   // "Mon".."Sun" — index 0 is always today
  char dateLabel[8] = {0};  // "Aug 1" — short month + day, for page headers
  float highC = 0.0f;
  float lowC = 0.0f;
  int16_t weatherCode = -1;  // Open-Meteo WMO code; see weather/WeatherIcons.h
  int8_t rainProbabilityPct = 0;
  float windSpeedMaxKmh = 0.0f;
};

// Plain, fixed-size snapshot of the Open-Meteo fetch (see
// weather/WeatherService.h). No String members — this crosses the
// background-fetch-task/main-loop boundary as a byte-for-byte copy, and a
// String's heap pointer would not survive that safely.
//
// `valid` is false until the first fetch ever succeeds; every other field
// is a zero/default until then, not placeholder data — callers must check
// `valid` before displaying these.
struct WeatherData {
  bool valid = false;
  float temperatureC = 0.0f;
  float feelsLikeC = 0.0f;
  int8_t humidityPct = 0;
  float pressureHpa = 0.0f;
  float windSpeedKmh = 0.0f;
  int16_t windDirectionDeg = 0;
  int8_t cloudCoverPct = 0;
  float visibilityKm = 0.0f;
  float uvIndex = 0.0f;
  int16_t weatherCode = -1;  // Open-Meteo WMO code; see weather/WeatherIcons.h
  bool isDay = true;
  char lastUpdate[8] = {0};     // "HH:MM", from the API's current.time
  char isoTime[20] = {0};       // full "YYYY-MM-DDTHH:MM" from current.time — see
                                // storage/WeatherHistoryManager.h, the only reader of this
  char locationName[40] = {0};  // AppSettings::locationName as of this fetch

  // Today's sunrise/sunset — appended to the existing daily=/forecast_days=
  // request (see weather/WeatherService.cpp's kDailyParams), not a separate
  // HTTP call. "HH:MM", local (same timezone=auto convention as lastUpdate).
  // Open-Meteo has no moon data at all; pages/PageSunMoon.cpp computes moon
  // phase/rise/set locally instead.
  char sunriseTime[6] = {0};
  char sunsetTime[6] = {0};

  // Next cfg::weather::HOURLY_FETCH_COUNT hours, same fetch/timestamp as the
  // fields above. hourlyCount may be less than HOURLY_FETCH_COUNT (or zero)
  // if the API returned a shorter/malformed hourly block — `valid` covers
  // the current-conditions fields only, so check hourlyCount separately.
  uint8_t hourlyCount = 0;
  HourlyForecastEntry hourly[cfg::weather::HOURLY_FETCH_COUNT];

  // Next cfg::weather::DAILY_FETCH_COUNT days, same fetch/timestamp as the
  // fields above. dailyCount may be less than DAILY_FETCH_COUNT (or zero) if
  // the API returned a shorter/malformed daily block.
  uint8_t dailyCount = 0;
  DailyForecastEntry daily[cfg::weather::DAILY_FETCH_COUNT];
};
