# Changelog

All notable changes to this project are documented here. Forked from
[HenrysCat/CYD-Weather-Dashboard](https://github.com/HenrysCat/CYD-Weather-Dashboard)
at v1.0.0; versions from 1.0.1 onward are local fixes on top of that base.

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
