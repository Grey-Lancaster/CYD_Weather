# Changelog

All notable changes to this project are documented here. Forked from
[HenrysCat/CYD-Weather-Dashboard](https://github.com/HenrysCat/CYD-Weather-Dashboard)
at v1.0.0; versions from 1.0.1 onward are local fixes on top of that base.
Patch is bumped by 4 on each release, carrying into minor odometer-style
once patch would reach double digits (e.g. `1.0.9` + one release =
`1.1.3`, not `1.0.13`).

## [1.0.9] - 2026-08-09

### Fixed
- Timezone field was free-text, and typing the intuitive-looking `"UTC-4"`
  silently produced a clock 8 hours off — POSIX TZ strings use inverted
  sign convention (`UTC-4` means 4 hours *ahead* of UTC, not behind), and a
  flat offset also has no DST rule so it'd drift wrong again every autumn.
  Replaced with a `<select>` of curated, known-correct POSIX TZ rule
  strings (same list/format as the sibling Claude_usage and
  TouchWifiProvisioner/CYD_RollingClock projects, sourced from
  https://github.com/nayarsystems/posix_tz_db) — removes the whole class
  of mistake and bakes in each zone's DST rule.

### Added
- 12-hour clock option (`AppSettings::hour12`, schema 13) — a checkbox in
  the web portal next to the timezone dropdown; status bar clock shows
  e.g. `9:29 AM` instead of `09:29` when enabled. Defaults to off (24-hour,
  matching prior behaviour).

## [1.0.5] - 2026-08-09

### Fixed
- Panel driver was misconfigured as ILI9341(_2) when this board revision
  (the "cyd2usb" variant, per
  [Grey-Lancaster/CYD-PhotoFrame](https://github.com/Grey-Lancaster/CYD-PhotoFrame))
  actually uses an ST7789 controller. The wrong driver still rendered a
  mostly-correct-looking image (the two chips' command sets overlap a lot),
  which is why it wasn't obviously broken — but colours were off. Switched
  `platformio.ini` to `ST7789_DRIVER`/`TFT_INVERSION_OFF` and flipped the
  matching runtime default (`cfg::defaults::INVERT_COLORS`); confirmed
  correct colours live on-device.

### Added
- `/mirror` — a web viewer (manual/auto-refresh) for showing the device's
  current screen without a camera, backed by `/mirror.bmp`. Reads the panel
  back over SPI (no framebuffer exists to read from directly) and streams a
  24-bit BMP. **Known issue**: captured colours don't match the real screen
  — this board's SPI pixel-readback doesn't correspond to true colour via
  any RGB/BGR/invert combination tried (confirmed even with chunking
  disabled, so it's not a multi-row read bug). Layout/text/icons are
  otherwise pixel-accurate, just mis-coloured. A real fix needs rendering
  into an off-screen sprite instead of reading hardware back (see
  `ScreenshotCapture.cpp`'s LVGL-flush-hook approach in the sibling
  `Claude_usage` project for the pattern) — deferred pending an injectable
  per-page draw target, a larger change than this release scope.

## [1.0.1] - 2026-08-09

### Fixed
- Hourly Forecast page collapsing to a single visible card for most of its
  shift cycle. `tick()`'s scroll-window math used the raw ~120-hour fetch
  count instead of the 12-hour display cap `render()` uses, letting the
  window scroll position advance past where `render()` clamps it. See
  [fix01.md](fix01.md) for the full root-cause writeup.

### Added
- Default WiFi credentials seeded into `AppSettings` on first boot
  (`cfg::net::DEFAULT_WIFI_SSID`/`DEFAULT_WIFI_PASSWORD` in
  `src/config/AppConfig.h`), so a freshly flashed board connects
  immediately instead of requiring the manual setup-AP flow.
