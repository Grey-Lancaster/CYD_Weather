# /weather

Live for **5 Day Forecast** (Page 1, default boot page), **Current
Conditions** (Page 2), **Hourly Forecast** (Page 3) and **Weather Trends**
(Page 4, plots the local history log via `WeatherHistoryManager` — no API
call of its own). System still renders non-weather (device/network) data.

- **`WeatherUnits.h`** — `TempUnit`/`WindUnit`/`PressureUnit` enums plus the
  conversions from Open-Meteo's native metric values (the only units
  `WeatherData` ever stores) to whichever unit `AppSettings::tempUnit`/
  `windUnit`/`pressureUnit` selects: `convertTempC()`/`tempUnitSuffix()`,
  `convertWindKmh()`/`windUnitSuffix()`, `convertPressureHpa()`/
  `pressureUnitSuffix()`. Every live page (`CurrentConditionsPage`,
  `ForecastPage`, `HourlyForecastPage`) reads the setting via
  `SettingsManager::instance().get()` and converts at render time — until
  2026-07-31 these settings were stored/exposed in the web portal but never
  actually applied to anything (a real bug the user hit: "changing wind
  units in web UI does not change on device"). Pressure shows 2 decimals for
  inHg specifically (integer rounding would show ~30 inHg almost always,
  since sea-level pressure barely varies outside 29-31 inHg) — every other
  unit/value stays a whole number, matching the rest of the UI's style.
- **`WeatherIcons.h/.cpp`** — the reusable weather condition/icon engine.
  One table (every Open-Meteo WMO `weather_code`, 0 through 99) drives four
  functions every page goes through instead of switching on weather_code
  itself: `getWeatherDescription()` (long, e.g. "Depositing Rime Fog"),
  `getShortDescription()` (short, e.g. "Fog" — for tight space like the
  Hourly Forecast table), `getWeatherIcon(code, isDay)` (returns a
  `WeatherIconId`; only Clear/Partly Cloudy actually change with `isDay`),
  and `getWeatherColour(code)` (a fixed, theme-independent RGB565 colour per
  broad category — rain is blue whether the UI is in dark or light mode).
  Deliberately has no dependency on `ui/` or
  TFT_eSPI — this is the weather domain's vocabulary, not how it's drawn.
  The actual icon art (`ui::drawWeatherIcon`) lives in
  `ui/WeatherIconTheme.h/.cpp`, which depends on `WeatherIconId`, not the
  other way round — a future icon theme (real artwork, a different style) is
  a matter of swapping that one file.
- **`WeatherData.h`** — the live snapshot:
  - `WeatherData` — current conditions (temperature, feelsLike, humidity,
    pressure, windSpeed, windDirection, cloudCover, visibility, uvIndex,
    weatherCode, isDay, lastUpdate, locationName) plus `hourly[]`/
    `hourlyCount`, the next `cfg::weather::HOURLY_FETCH_COUNT` (24) hours
    from the *same* fetch — there's no separate hourly timestamp/location,
    since it's one HTTP request. `valid` covers the current-conditions
    fields only; check `hourlyCount` separately before reading `hourly[]`.
  - `HourlyForecastEntry` — one forecast hour: time ("HH:MM"), temperature,
    weatherCode, rainProbability, windSpeed, isDay (per-hour, so overnight
    hours correctly draw the moon/night icon variant instead of the sunny
    one — Open-Meteo via its own `hourly=...,is_day` field, Met.no via its
    symbol_code's `_day`/`_night` suffix; see `WeatherService.cpp`). Index 0
    is the current/nearest hour ("now") — `HourlyForecastPage` highlights it
    the same way `ForecastPage` highlights today.
  - `DailyForecastEntry` — one forecast day: `dayLabel` ("Mon".."Sun") and
    `dateLabel` ("Aug 1"), both precomputed at parse time from Open-Meteo's
    date string (see `WeatherService.cpp`'s `dayOfWeekIndex()`/
    `formatDayLabels()` — Sakamoto's algorithm; there's no on-device clock,
    so the weekday is derived entirely from the date string), plus
    highC/lowC, weatherCode, rainProbability, windSpeedMax. Index 0 is
    always *today* — Open-Meteo's `daily.time[0]` is today in the requested
    timezone, so no date comparison is needed to find/highlight it.
  - Fixed-size, no `String` members — the struct crosses the fetch-task/
    main-loop boundary as a raw copy.
- **`WeatherService.h/.cpp`** — owns the Open-Meteo fetch: a single request
  returns `current=`, `hourly=` (`forecast_hours=24`) and `daily=`
  (`forecast_days=5`) together in one HTTP GET, parsed by one background
  FreeRTOS task so the main loop is never stalled; `tick()` (cheap, called
  every second — `cfg::sched::WEATHER_TICK_MS`) just checks timers and polls
  whether that task finished. Refetches every `AppSettings::weatherRefreshS`
  (default 15 min, already a live web-portal setting) — this single shared
  cadence comfortably covers the Hourly Forecast page's "refresh every 30
  min" requirement too, so there's no second timer/request. On failure, the
  last successful `WeatherData` is kept and `lastFetchFailed()` flips true,
  driving every live page's offline badge (`ui::drawWarningBadge`,
  ui/Widgets.h).
  - **Gotcha (fixed 2026-07-31):** Open-Meteo replies with
    `Transfer-Encoding: chunked` (no `Content-Length`). `HTTPClient::getStream()`
    hands back the *raw* chunked bytes — chunk-size headers included —
    `HTTPClient` only de-chunks internally for `getString()`/`writeToStream()`.
    Parsing `getStream()` directly still "succeeds" (no parse error) but
    silently drops keys, which is why the very first live build showed `--`
    on both pages despite a real 200 response. `parseResponse()` now takes
    the de-chunked `getString()` result instead. If a future step goes back
    to parsing directly off the socket for a bigger response, this needs
    re-solving properly (e.g. a small de-chunking `Stream` wrapper) rather
    than reverting to raw `getStream()`.
- **`WeatherTrendsPage`** (Page 4, in `src/pages/`) draws from
  `WeatherHistoryManager::getHistory()` only — no request of its own, no
  separate storage. It converts temperature/wind through the same
  `WeatherUnits.h` functions as every other live page, so a unit change in
  settings applies there too. Its actual line-graph drawing lives in
  `ui/Graph.h/.cpp`, kept generic (raw `float*`/count in, no page or history
  dependency) so a future page can reuse it. `SystemPage` is unrelated to
  weather data and stays placeholder-free by design (it's live
  device/network info).
- **`pages/HourlyForecastPage.h/.cpp`** (Page 3) — rewritten from a dense
  12-row table into a card-based timeline matching `ForecastPage`'s visual
  language (rounded, banded-gradient cards, same header pattern) rather than
  the 5-day layout itself: only `kVisibleCards` (6) of the 12 fetched hours
  are shown at once, since 12 don't fit at a readable size, and the visible
  window automatically shifts forward by one hour every 4s (`tick()`,
  `_windowStart`/`_lastShiftMs`) — no swipe/touch involved, so it never
  conflicts with `PageManager`'s own swipe-to-change-page gesture. The
  soonest visible hour (index 0) is highlighted the same way `ForecastPage`
  highlights today.
- **`pages/CurrentConditionsPage.h/.cpp`** (Page 2) — replaced the earlier
  `CurrentWeatherPage` (same slot/purpose, fuller field set): hero
  temperature in TFT_eSPI's built-in 48px "7-segment" font (`setTextFont(7)`
  — digits/minus only, so only the numeric part uses it; the "C" suffix is
  drawn in the theme's normal font), an 8-stat grid (feels like, humidity,
  pressure, wind speed, wind direction as an 8-point compass label, cloud
  cover, visibility in km, UV index), current date/time (derived from
  `current.time`/`daily[0]` — no on-device clock, so "Current Time" and
  "Last Update Time" are necessarily the same instant, just captioned
  differently), and the offline badge. The small "WiFi indicator" the page's
  original task asked for is already provided globally by `StatusBar` (shown
  above every page), so this page doesn't duplicate it.
- **Not yet used**: `AppSettings::timezone` — the fetch requests
  `timezone=auto` instead, since the stored value is a POSIX TZ string and
  Open-Meteo's `timezone` parameter expects an IANA name or `auto`.
