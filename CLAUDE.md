# CLAUDE.md

CYD Weather: firmware that turns an ESP32-2432S028R "Cheap Yellow Display" (2.8" 320x240, plus a 3.5" 480x320 variant) into a standalone weather station: live conditions and forecasts from 20 selectable providers, on-device history and trend graphs, sun/moon, alerts, and a built-in web portal for setup. Public repo, forked from HenrysCat/CYD-Weather-Dashboard (credit stays in the README). Current version 2.0.0 (`src/config/Version.h`). `README.md` is the user guide and has an architecture section; read it before large changes.

## Layout (`src/`)
- `main.cpp` - setup and a cooperative loop that only runs the `TaskScheduler` (no `delay()` anywhere).
- `config/` - `AppConfig.h` (every pin, timeout and default), `Version.h`.
- `display/` - `DisplayManager` (TFT_eSPI, backlight PWM, shared sprite canvas), `DisplayConfig`, `TouchManager`.
- `network/` - `WifiProvisioning` (bridges TouchWifiProvisioner/LVGL onto this project's own display and touch), `NetworkManager`, `TimeManager` (SNTP).
- `pages/` - `Page` base class, `PageManager`, one file pair per dashboard page, `SystemPage`.
- `storage/` - `SettingsManager` (versioned NVS blob), `WeatherHistoryManager` (LittleFS CSV log), SD card, settings backup/console.
- `ui/` - theme, status bar, shared widgets, weather icons, alert theme, `Graph`.
- `weather/` - `WeatherService` (multi-provider fetch), icon engine, alert manager, units. `src/weather/README.md` documents the data layer.
- `web/` - `WebPortal` and the PROGMEM portal page.
- `include/lv_conf.h`, `partitions.csv`, `docs/` (screenshots and a short note on a past hourly-forecast fix), `CHANGELOG.md`.

## Build / flash
- PlatformIO. One env per board: `cyd` (ILI9341), `cyd2usb` (ST7789, the default env), `cyd2b` (ILI9341 inverted), `cyd35` (ST7796 3.5"). Wrong env gives a white or scrambled screen, nothing is damaged.
- `pio run -e <env>` builds; `pio run -e <env> -t upload` flashes; `pio device monitor` is 115200 baud.
- Platform (`espressif32@6.10.0`) and all libraries are pinned; do not bump the platform, Arduino-ESP32 3.x does not build this. TFT_eSPI is configured entirely from build flags, never edit the library.
- I found no unit tests. Verification is building and flashing a board; do not claim hardware behaviour is verified unless it was seen on a board.

## Gotchas
- `partitions.csv` is a dual-app OTA layout (portal "Firmware update" takes the plain `*-firmware.bin`). A board on an older layout needs one `*-factory.bin` flash, which resets the weather history log. The comment block near `board_build.partitions` in `platformio.ini` still says there is no OTA; it is out of date, trust `partitions.csv` and the README.
- LVGL (8.x only) is deliberately used only for the pre-connect WiFi picker; the rest of the UI is TFT_eSPI with no LVGL. TouchWifiProvisioner is pinned by commit in `lib_deps`.
- 3.5" board: backlight on GPIO27 and touch shares the display SPI pins, so touch is read through TFT_eSPI; the 320x240 UI is drawn centred on the larger panel (`cfg::display::ORIGIN_X/Y`); needs a one-time four-corner touch calibration.
- Orientation, colour order, mirror and inversion are runtime settings in the portal; each env only sets first-boot defaults (`cfg::defaults` in `AppConfig.h`).
- Backlight (GPIO21) belongs to `DisplayManager`, so `-DTFT_BL` is intentionally not set for the 2.8" envs.

## Release / deploy
- Bump `Version.h` and add a `## [x.y.z]` section to `CHANGELOG.md`. Patch goes up by 4 per release and carries into minor odometer-style (rule is at the top of the changelog).
- CI (`.github/workflows/build.yml`) builds all four envs on every push. Pushing any tag (e.g. `2.0.0` or `v2.0.0`; previous tags were bare, release candidates like `2.0.0-rc6`) publishes a GitHub Release with `CYD-Weather-<env>-factory.bin` and `CYD-Weather-<env>-firmware.bin`; the notes are the matching CHANGELOG section.
- The web installer is the `gh-pages` branch (`index.html`, per-board manifests `*.json` and `CYD-Weather-<env>.bin`). I did not find how those files are refreshed from a release; check before assuming.
- Do not tag or publish a release unless asked.
