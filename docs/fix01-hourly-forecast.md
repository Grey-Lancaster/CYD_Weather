# Fix 01: Hourly Forecast collapses to one visible card

## Symptom

The Hourly Forecast page (page 3) shows only a single card (e.g. stuck on
`18:00`) instead of scrolling through a 6-card window across the fetched
hours, for most of the shift cycle.

## Root cause

`HourlyForecastPage` fetches up to 120 hours from Open-Meteo
(`cfg::weather::HOURLY_FETCH_COUNT`), but only *displays* the first 12
(`cfg::weather::HOURLY_DISPLAY_COUNT`) — `render()` caps `total` to that
limit before laying out cards.

`tick()`, however, computed how far the 6-card viewing window is allowed to
scroll (`maxStart`) from the raw, uncapped fetch count (`_hourlyCount`, up to
120) instead of the display-capped total:

```cpp
// src/pages/HourlyForecastPage.cpp — tick(), before the fix
_hourlyCount = _weather.data().hourlyCount;   // up to 120
...
const uint8_t maxStart = _hourlyCount > kVisibleCards ? _hourlyCount - kVisibleCards : 0; // up to 114
```

So `_windowStart` climbs as high as 114 as the page auto-shifts. Meanwhile
`render()` clamps the start position to the last *displayable* hour:

```cpp
// src/pages/HourlyForecastPage.cpp — render()
const uint8_t total = min(d.hourlyCount, cfg::weather::HOURLY_DISPLAY_COUNT); // 12
const uint8_t start = min(_windowStart, (uint8_t)(total > 0 ? total - 1 : 0)); // clamped to 11
const uint8_t visible = min((uint8_t)(total - start), kVisibleCards);
```

Once `_windowStart` exceeds 11, `start` stays pinned at 11 (`total - 1`), and
`visible` collapses to `total - start` = `12 - 11` = **1**. Since
`_windowStart` only wraps back to 0 after cycling through all the way to
`maxStart` (114), the page sits at `visible == 1` for roughly 103 of every
115 shift ticks (~90% of the time) before briefly showing the full window
again near the wrap-around.

## Fix

Cap `_hourlyCount` to the same display limit `render()` already uses, so the
window-scroll math in `tick()` never advances past the hours actually shown:

```cpp
// src/pages/HourlyForecastPage.cpp — tick(), after the fix
// Capped to the same display limit render() uses — tick()'s window-scroll
// math below must stay within the hours actually shown, or _windowStart
// can advance past render()'s clamp point and collapse the visible
// window to a single card for most of the shift cycle.
_hourlyCount = min(_weather.data().hourlyCount, cfg::weather::HOURLY_DISPLAY_COUNT);
```

`maxStart` is now computed against `total` (max 12), so `_windowStart` stays
within `0..(12 - kVisibleCards)` = `0..6`, matching what `render()` can
actually display.

## Files changed

- [src/pages/HourlyForecastPage.cpp](src/pages/HourlyForecastPage.cpp) — the fix above.

## Verification

Built (`pio run -e cyd`), flashed to the physical board over USB, confirmed
clean boot via serial monitor (hash verified, normal startup log, WiFi
connected, weather fetched). Visual confirmation on-device that the Hourly
Forecast page now cycles through multiple cards pending.
