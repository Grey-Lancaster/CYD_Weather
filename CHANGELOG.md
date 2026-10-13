# Changelog

All notable changes to this project are documented here. Forked from
[HenrysCat/CYD-Weather-Dashboard](https://github.com/HenrysCat/CYD-Weather-Dashboard)
at v1.0.0; versions from 1.0.1 onward are local fixes on top of that base.
Patch is bumped by 4 on each release, carrying into minor odometer-style
once patch would reach double digits (e.g. `1.0.9` + one release =
`1.1.3`, not `1.0.13`).

## [Unreleased]

## [2.0.0] - 2026-10-13

The modernised release: one build per board (including the new 3.5" CYD), pinned dependencies,
touch calibration, a watchdog, over-the-air updates, a browser installer and a settings portal
with screenshot and touch test. This version rolls up everything from the 2.0.0 release
candidates (rc1 to rc6, below); the 3.5" build was confirmed on real hardware by a tester.

### Changed
- 3.5" board: *Mirror display* is ignored and hidden in the portal, like *Rotate display 90*
  (the panel reads correctly as shipped).
- The firmware now reports its real version (2.0.0) on the status page and in `/api/status`.

## [2.0.0-rc6] - 2026-10-13

Sixth release candidate: a Touch test page, and the 3.5" screenshot is no longer shifted.

### Added
- **Touch test** page (portal button, `/touch`): shows where the board thinks each tap landed,
  as a dot on a copy of the dashboard, with the numbers before and after the orientation
  correction. For checking touch alignment after changing flip or mirror. Feed: `/api/touch`.

### Fixed
- 3.5" board: the screenshot was shifted by the centring offset (80x40 pixels) and cut off on the
  right and bottom; it now reads from the dashboard's own corner of the panel.

## [2.0.0-rc5] - 2026-10-12

Fifth release candidate: touch follows the Mirror display setting too, a new WiFi library (hidden and saved networks), and a fix for the 3.5" screenshot.

### Fixed
- 3.5" board: the Screenshot / `/mirror.bmp` came out garbled (repeated, mis-coloured bands).
  The ST7796 returns 2 bytes per pixel in its 16-bit mode, so the panel is switched to its
  18-bit mode (3 bytes per pixel) for the capture and back. Untested on the 3.5" hardware.

### Changed
- Switching **Mirror display** no longer needs a new touch calibration either (it joins the
  180° flip): the stored calibration follows it live. Only the 90° setting still asks again.
- WiFi provisioning library updated to 1.5.0 (pinned): a **Hidden network...** row in
  the picker, and up to five saved networks with automatic reconnection to whichever
  is in range. A network saved by the older version is carried over automatically.

## [2.0.0-rc4] - 2026-10-12

Fourth release candidate: flipping the screen 180° no longer needs a new touch calibration.

### Changed
- Flipping the screen 180° no longer needs a new touch calibration: the stored one is
  reused with both axes reversed, live (no restart). Mirror changes still ask for a
  new calibration, since they are not a simple reversal. Applies to all boards.

## [2.0.0-rc3] - 2026-10-11

Third release candidate: fixes the 3.5" display rotation problem found by the first tester.

### Fixed
- 3.5" board: "Rotate display 90°" shifted the dashboard to the top-right and cut it off
  (reported by a tester). That setting only compensates a wiring quirk of some 2.8"
  boards, so it is now ignored on the 3.5" build and hidden in the portal.

## [2.0.0-rc2] - 2026-10-11

Second release candidate. Adds over-the-air updates (boards on an older version need one `-factory` flash first), swipe-only page navigation, and fixes for the status bar and the Trends title.

### Changed
- **Pages change by swiping only.** A tap anywhere no longer jumps to the next page;
  taps operate controls (forecast cards, graph tabs) and restart the auto-rotate timer.

### Added
- **Over-the-air firmware update** in the web portal ("Firmware update"). Uses a
  dual-app partition table, so a board that ran an older version needs one
  `-factory` flash first (this resets the weather history log). The factory images
  now also carry the boot-slot table.

## [2.0.0-rc1] - 2026-10-11

Release candidate: adds the 3.5" CYD (`cyd35`) build for testing, plus a modernised, pinned build for every board.

### Added
- **Screenshot** button in the web portal (opens the existing screen mirror) with a
  **Save PNG** download, for troubleshooting and sharing.
- **"Configure it here" screen** for 15 seconds right after the first WiFi
  connection, showing the device's IP address and hostname (tap to skip), and a
  Hostname row on System Information (BOOT button), so the settings portal is easy
  to find. The README explains both.
- **Touch calibration for the 2.8" boards:** five on-screen crosshairs, then a
  least-squares fit of a full 2D transform (so any axis direction, inversion or
  skew of the touch panel is handled), stored in NVS with the display orientation
  it was made in. Runs automatically on first start of the ILI9341 builds (their
  touch panels differ between board revisions: on one board the original
  calibration put taps on the wrong row of the WiFi keyboard); the ST7789 build
  keeps its original calibration unless you press *Recalibrate touch* in the web
  portal (new, on every board). The WiFi picker follows the app's orientation once
  a calibration exists.
- **Watchdog:** if the main loop ever stops running for 60 seconds, the
  device restarts instead of freezing.
- **Last restart reason** (power on, reset button, software restart, crash,
  watchdog, brownout) on the System Information page, in `/api/status`
  (`lastRestart`) and the web portal's Status card, and in the boot log — so
  crashes can be spotted without a serial cable.
- **Board variants:** `platformio.ini` now has one environment per panel type —
  `cyd` (ILI9341), `cyd2usb` (ST7789, the previous and default build) and
  `cyd2b` (ILI9341, inverted colours) — with matching first-boot orientation
  and inversion defaults, so ILI9341 boards no longer need build-file edits.
- GitHub Actions builds every board on each push and, for a tag, publishes a
  release with the flashable images (`CYD-Weather-<env>-factory.bin`).

- **3.5" CYD support** (`cyd35` env, ST7796 480×320, ESP32-32E pinout): backlight
  on GPIO27, touch read through TFT_eSPI on the TFT's shared SPI bus with an
  on-device four-corner calibration (web portal: *Recalibrate touch*), and the
  existing 320×240 UI shown centered on the larger panel via a TFT_eSPI
  viewport. Written from a user's pin list; not yet tested on hardware.

### Changed
- All dependencies and the PlatformIO platform are pinned to the versions
  this project is built and tested with (previously `lvgl@^8.3.11` had drifted
  to 8.4.0 and `TouchWifiProvisioner` tracked the git branch head).
- README: install instructions for the release files; removed the link to the
  upstream project's web flasher (it installs the upstream firmware, not this
  fork's); corrected the panel description (ILI9341 *or* ST7789).
- `fix01.md` moved to `docs/`.

### Fixed
- Weather Trends: the graph name in the title row no longer overlaps the previous one when the graph changes.
- Status bar: with a weather alert active, the location, date and time were drawn on a bright blue background (the alert icon left its text colours behind).
- **Touch died as soon as the web portal was opened** (and stayed dead until a
  reboot) on boards with an SD card in the slot. The portal page checks for an SD
  card on load; that borrows the VSPI peripheral the touch controller shares, and
  `SPIClass::begin()` is a no-op while the object thinks the bus is still up, so
  the touch pins were never re-attached afterwards. `TouchManager::reacquireBus()`
  now calls `end()` first. Verified on hardware (touch works after the SD check).
  This may also explain some "touch stops responding" reports.
- Touch could stop responding for good after a WiFi outage: the touch driver
  only read the controller after an interrupt edge on GPIO36 and never
  recovered if one edge was missed. It now samples the controller on every
  poll, so there is no interrupt state that can get stuck. (Not reproducible
  on demand, so unconfirmed against the original report.)

## [1.4.5] - 2026-08-10

### Added
- Diagnostic logging for a rare, hard-to-reproduce bug: touch stops
  responding (dim, unresponsive to taps, "half not working") after a WiFi
  outage-and-recovery cycle, but the main scheduler/loop stays completely
  healthy throughout — confirmed live on hardware that only a reset (not
  necessarily a full power cycle) recovers it, and heap/uptime/page rotation
  never falter while it's broken. `NetworkManager` now logs outage duration
  and reconnect count at Info level (was Debug-only); the existing
  `sys.stats` heartbeat (every 60s) now also logs touch's live press count
  and `_ok` state alongside WiFi state, so next occurrence can be
  cross-referenced against the WiFi outage timeline without needing Debug
  logging enabled in advance. No behaviour change — logging only. Working
  theory (unconfirmed — needs a scope/multimeter on the 3.3V rail during a
  live reconnect storm to verify): the WiFi radio's more aggressive
  reconnect-attempt current draw during an outage sags the shared 3.3V rail
  enough to glitch the touch controller's analog front-end, without dipping
  low enough to trip the ESP32's own brownout detector.

## [1.4.1] - 2026-08-10

### Fixed
- Light theme showed a faint mottled/banded texture behind the 5-Day
  Forecast, Hourly Forecast, and per-day hourly-drilldown cards, confirmed
  on the physical screen (not a `/mirror` capture artifact — its SPI
  readback bug is separate and already documented). Root cause: those
  cards' background is a hand-rolled vertical gradient (TFT_eSPI has no
  native gradient primitive), blended per-row in RGB565. Light theme's
  `surface`/`bg`/`textPrimary` sit close enough together that the gradient's
  top/bottom colours only span a handful of distinct 5/6/5-bit levels over a
  ~150px-tall card — those few levels repeating down the card is what read
  as a dirty/mottled texture. Every other (dark-background) theme has enough
  spread for the same blend to look continuous, so this is a one-theme
  special case (`activeTheme() == AppTheme::Light` falls back to a flat
  `fillRoundRect` instead of the banded loop) rather than a general
  contrast-based heuristic, which isn't reliable to tune without live
  hardware testing per theme.

## [1.3.7] - 2026-08-10

### Fixed
- Root-caused the WiFi picker's persistent touch/display mismatch (1.2.5,
  1.2.9, 1.3.3 all missed it): `TouchWifiProvisioner`'s own tested-working
  example (`CYD_BasicConnect.ino`) drives this exact board with a plain
  `tft.setRotation(3)` / `ts.setRotation(3)`, no MADCTL mirror bits at all —
  and this project's stored touch calibration values are *exactly* that
  example's (`200..3700` / `240..3800`), meaning they were calibrated
  against that simple scheme, not against this app's own
  `displayFlipped`/`mirrorH`/`mirrorV`-driven MADCTL composition. Every
  attempt so far tried to reconcile the picker with this app's flip/mirror
  settings math, when the calibration data never matched that math to begin
  with. `WifiProvisioning::begin()` now forces the proven rotation-3 scheme
  directly (`TouchManager::setProvisioningRotation()` +
  `rawTouchProvisioning()`, bypassing `mapRawToScreen()` entirely),
  independent of whatever this app's own display settings are; `startApp()`
  restores the app's configured orientation once connected.

## [1.3.3] - 2026-08-10

### Fixed
- 1.2.9's touch fix still landed taps on the wrong horizontal position with
  `displayFlipped` + `mirrorH` both on (confirmed live: typing "m" hit "x"
  and vice versa on the on-device keyboard) — the MADCTL-bit-algebra model
  it used assumes MX/MY keep their portrait-native meaning (MX=horizontal,
  MY=vertical), but this panel's MV bit (always set, since the whole project
  only ever runs landscape) swaps which visual axis each one actually
  controls. `TouchManager::mapRawToScreen()` now cross-wires the two
  settings to match what's actually observed on screen: `mirrorV` drives the
  X-axis touch decision, `mirrorH` drives the Y-axis one.

## [1.2.9] - 2026-08-10

### Fixed
- Touch position drifted out of sync with the screen whenever both
  `displayFlipped` and `mirrorH`/`mirrorV` were enabled together (the
  combination introduced by 1.2.5's new defaults). `TouchManager` used to
  hand the 180° half of this off to the XPT2046 driver's own
  `setRotation()`, which inverts the raw ADC reading *before* calibration is
  applied — while `mirrorH`/`mirrorV` mirrored the already-calibrated screen
  coordinate *after*. The two only agreed when the touch calibration
  happened to be symmetric around the ADC's midpoint, which it isn't (see
  the cal values logged at boot). `mapRawToScreen()` now applies flip and
  mirror together, in calibrated screen space, using the same bit algebra as
  the display's own MADCTL composition (`DisplayConfig::buildMadctl()`) —
  each axis mirrors iff `displayFlipped != mirror{H,V}` — so a tap always
  lands under the pixel it's touching for any combination of these settings.

## [1.2.5] - 2026-08-10

### Changed
- Default display orientation is now mirrored horizontally and flipped 180°
  (`cfg::defaults::MIRROR_H`/`DISPLAY_FLIPPED` in `src/config/AppConfig.h`),
  to match how this board is physically mounted. Both settings already
  existed and were live-adjustable via the web portal, but the on-device
  WiFi setup picker (`WifiProvisioning`) runs before the portal exists to
  change them, so it always rendered with whatever the *defaults* were —
  changing the defaults is what actually fixes the picker screen; it also
  changes the main app's out-of-box orientation to match. Touch mapping
  (`TouchManager::mapRawToScreen()`) already honoured these same settings,
  so taps still line up with the mirrored/flipped picker.

## [1.2.1] - 2026-08-10

### Fixed
- Sun & Moon's Sunrise/Sunset/Moonrise/Moonset times (and the daylight bar's
  end labels) ignored the 12-hour clock setting, same underlying issue as
  1.1.7's Hourly Forecast/"Updated" fix — now reformatted via
  `fmt::clockTime()` at display time, same as everywhere else. The
  underlying parsed minutes-of-day used for the daylight-bar math and
  remaining-daylight countdown are computed before this reformatting, so
  the fix is display-only.

## [1.1.7] - 2026-08-09

### Fixed
- Weather alert headlines (Heat Warning, Cold Warning, Ice Risk) always
  showed Celsius regardless of `AppSettings::tempUnit` — they now use the
  same `convertTempC`/`tempUnitSuffix` conversion every page already uses.
- Hourly Forecast card times and every "Updated HH:MM" label ignored the
  12-hour clock setting added in 1.0.9 — they read `hour12` now (new
  `fmt::clockTime()` helper in `utils/Format.h`). The underlying stored
  time strings stay 24-hour always (day-boundary detection and sun/moon
  time math parse them directly) — only the point of drawing reformats.

### Known issue
- Sun & Moon's Sunrise/Sunset/Moonrise/Moonset times are still always
  24-hour, same underlying bug — not covered by this fix.

## [1.1.3] - 2026-08-09

### Changed
- Replaced the AP-mode/captive-portal WiFi setup flow with an on-device
  touchscreen picker and keyboard —
  [TouchWifiProvisioner](https://github.com/Grey-Lancaster/TouchWifiProvisioner),
  a separate library built on LVGL. LVGL is scoped entirely to this brief
  pre-connect phase (see `network/WifiProvisioning.cpp`); the rest of the
  app still draws directly via TFT_eSPI, unchanged.
- WiFi credentials moved out of `AppSettings` entirely (schema 14) —
  TouchWifiProvisioner owns them in its own NVS namespace now, so there's
  exactly one place they can live instead of two that could drift out of
  sync. The web portal's WiFi SSID/password fields are gone; set WiFi
  on-device instead.
- `SetupPage` removed (it only ever pointed at the old captive portal).
  `NetworkManager` slimmed to a thin WiFi-status mirror — it no longer
  drives connecting/retrying/AP-fallback, since TouchWifiProvisioner does
  all of that (including reconnecting on its own after a drop) before the
  rest of the app even starts.
- Factory reset (BOOT button, 10s hold) now also clears TouchWifiProvisioner's
  saved credentials, not just `AppSettings` — previously they'd have
  survived a factory reset as a surprising leftover in a separate NVS
  namespace.

### Fixed
- Removed the `shop2`/baked-in default WiFi credentials added earlier this
  series (meant as a local one-off convenience, but got committed and
  pushed to this public repo along with an unrelated fix — rotate that
  router password if you haven't already). On-device provisioning replaces
  the need for any baked-in default.

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
