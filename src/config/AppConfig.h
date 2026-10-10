#pragma once

#include <Arduino.h>

// ============================================================================
// Central compile-time configuration. Every pin, size, timeout, capacity and
// default lives here. Values a user may want to change at runtime also exist
// in AppSettings (storage/SettingsManager.h) and only take their *defaults*
// from cfg::defaults below.
// ============================================================================
namespace cfg {

namespace serial {
constexpr uint32_t BAUD = 115200;
}

namespace log {
constexpr size_t LINE_BUFFER = 256;
constexpr uint8_t DEFAULT_LEVEL = 3;  // 0=None 1=Error 2=Warn 3=Info 4=Debug
}

namespace pins {
// TFT data/control pins are configured via TFT_eSPI build flags (platformio.ini).
#if defined(BOARD_CYD35)
constexpr int8_t TFT_BACKLIGHT = 27;  // 3.5" board
#else
constexpr int8_t TFT_BACKLIGHT = 21;
#endif
constexpr int8_t TOUCH_CLK = 25;
constexpr int8_t TOUCH_MISO = 39;
constexpr int8_t TOUCH_MOSI = 32;
constexpr int8_t TOUCH_CS_PIN = 33;  // note: TOUCH_CS itself is a TFT_eSPI build-flag macro
constexpr int8_t TOUCH_IRQ = 36;
// The board's physical BOOT button — also GPIO0, the boot-mode strapping
// pin, but that only matters during the reset/boot sequence itself; reading
// it as a plain INPUT_PULLUP button once setup() is running is standard
// practice and doesn't interfere with strapping. See main.cpp's
// "boot.button" scheduler task.
constexpr int8_t BOOT_BUTTON = 0;
// microSD slot, on this board's other SPI-capable pin set (its own VSPI
// peripheral mapping, distinct from the touch controller's — see
// storage/SdCardManager and TouchManager::reacquireBus()). Standard wiring
// for the ESP32-2432S028R; unverified against this specific unit's silkscreen.
constexpr int8_t SD_CS = 5;
constexpr int8_t SD_SCK = 18;
constexpr int8_t SD_MISO = 19;
constexpr int8_t SD_MOSI = 23;
}

namespace display {
// The UI's logical size: every page is laid out for this 320x240 landscape frame.
constexpr uint16_t WIDTH = 320;
constexpr uint16_t HEIGHT = 240;
// The physical panel in landscape. Bigger than the UI on the 3.5" board, where the
// UI is shown centered (DisplayManager sets a TFT_eSPI viewport at ORIGIN_X/Y and
// TouchManager subtracts the same offset).
#if defined(BOARD_CYD35)
constexpr uint16_t PANEL_W = 480;
constexpr uint16_t PANEL_H = 320;
#else
constexpr uint16_t PANEL_W = 320;
constexpr uint16_t PANEL_H = 240;
#endif
constexpr int16_t ORIGIN_X = (int16_t)((PANEL_W - WIDTH) / 2);
constexpr int16_t ORIGIN_Y = (int16_t)((PANEL_H - HEIGHT) / 2);
// This UI's layout (ui/Widgets.h) is fixed for a 320x240 landscape frame, so
// only the two landscape TFT_eSPI rotations are supported — portrait (0/2)
// is not a page-manager orientation and there is no code path for it.
constexpr uint8_t ROTATION = 1;          // landscape, USB on the right — default orientation
constexpr uint8_t ROTATION_FLIPPED = 3;  // landscape, USB on the left (180° flip)
constexpr uint8_t COLOR_ORDER_COUNT = 2;  // valid AppSettings::colorOrder values
constexpr uint8_t MIN_BRIGHTNESS = 5;  // floor so the panel can never go fully dark
constexpr uint8_t BL_LEDC_CHANNEL = 0;
constexpr uint32_t BL_LEDC_FREQ_HZ = 5000;
constexpr uint8_t BL_LEDC_RES_BITS = 8;
// Largest permitted off-screen sprite (16 bpp): 320x80 px = 51200 bytes.
// A full-screen buffer (150 KB) would exceed the largest free heap block.
constexpr uint32_t CANVAS_MAX_BYTES = 52u * 1024u;
}

namespace touch {
constexpr uint32_t TAP_MAX_MS = 500;
constexpr uint16_t SWIPE_MIN_DX = 40;  // px — minimum horizontal travel to register as a swipe
// XPT2046 raw-value calibration defaults (persisted, adjustable via API).
constexpr uint16_t RAW_X_MIN = 200;
constexpr uint16_t RAW_X_MAX = 3700;
constexpr uint16_t RAW_Y_MIN = 240;
constexpr uint16_t RAW_Y_MAX = 3800;
}

namespace web {
constexpr uint16_t PORT = 80;
constexpr const char* PASS_MASK = "********";
constexpr uint32_t RESTART_DELAY_MS = 600;  // lets the HTTP response flush first
constexpr uint32_t APPLY_DELAY_MS = 800;    // settings apply after the response is sent
}

namespace sched {
constexpr uint8_t MAX_TASKS = 16;
constexpr uint32_t TASK_BUDGET_WARN_MS = 50;
// Task cadences.
constexpr uint32_t WEB_HANDLE_MS = 0;  // 0 = every loop pass
constexpr uint32_t TOUCH_POLL_MS = 20;
constexpr uint32_t BOOT_BUTTON_POLL_MS = 20;  // same cadence as touch.poll
constexpr uint32_t RENDER_MS = 50;
constexpr uint32_t UI_TICK_MS = 250;
constexpr uint32_t NET_TICK_MS = 500;
// Cheap: either no sync is pending/due yet, or it's just checking
// time(nullptr) against a threshold (see network/TimeManager.h).
constexpr uint32_t TIME_TICK_MS = 1000;
constexpr uint32_t STATS_MS = 60000;
constexpr uint32_t CONSOLE_POLL_MS = 20;  // serial test-command console
// Cheap: either it's not yet time for a refetch, or the background fetch
// task is already running and this just polls a flag (see WeatherService).
constexpr uint32_t WEATHER_TICK_MS = 1000;
// Just a version-counter comparison unless a fetch actually landed (see
// WeatherHistoryManager) — doesn't need to be any faster than the fetch
// cadence itself.
constexpr uint32_t HISTORY_TICK_MS = 5000;
// Same reasoning as HISTORY_TICK_MS: just a version-counter comparison
// unless a fetch actually landed (see weather/WeatherAlertManager.h) — the
// real evaluation work only runs on that edge, never on a timer.
constexpr uint32_t ALERTS_TICK_MS = 5000;
}

namespace ui {
constexpr uint8_t MAX_PAGES = 9;
constexpr uint8_t STATUS_BAR_H = 24;
constexpr uint32_t PAGE_DATA_REFRESH_MS = 1000;
// Auto page rotation (cycles the rotatable pages while idle).
constexpr bool DEFAULT_AUTO_ROTATE = true;
constexpr uint16_t DEFAULT_PAGE_DURATION_S = 10;
constexpr uint16_t PAGE_DURATION_MIN_S = 3;
constexpr uint16_t PAGE_DURATION_MAX_S = 3600;
// Hourly Forecast page (see pages/HourlyForecastPage.cpp): how long each
// visible window of cards stays on screen before the timeline shifts forward
// by one hour.
constexpr uint16_t DEFAULT_HOURLY_SHIFT_S = 4;
constexpr uint16_t HOURLY_SHIFT_MIN_S = 1;
constexpr uint16_t HOURLY_SHIFT_MAX_S = 30;
// Page-switch fade: backlight ramps down then back up across the swap so the
// redraw underneath is never visible (see PageManager's transition state
// machine) — each leg (fade-out, fade-in) runs for this long.
constexpr uint32_t PAGE_TRANSITION_MS = 180;
}

namespace sys {
constexpr uint32_t LOW_HEAP_WARN_BYTES = 40u * 1024u;
// Main-loop watchdog. Generous: the WiFi picker/connect path can legitimately
// spend a while inside one loop() pass; a real hang is far longer than this.
constexpr uint32_t WATCHDOG_S = 60;
// How long the physical BOOT button must be held to trigger a factory reset
// (see main.cpp's "boot.button" scheduler task). Long enough that a normal
// short press (jump to System Info) or an accidental bump can't reach it.
constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;
}

namespace storage {
constexpr const char* NVS_NAMESPACE = "cyd";
constexpr const char* SETTINGS_KEY = "settings";
constexpr uint32_t SETTINGS_MAGIC = 0x43594431;  // "CYD1"
// Bump on every AppSettings layout change. SettingsManager migrates forward
// from any older schema it recognizes (see storage/SettingsManager.cpp) so
// existing installs keep their WiFi credentials and other settings.
constexpr uint16_t SETTINGS_SCHEMA = 14;
}

namespace sdcard {
// SD's own SPI clock — well under the TFT's 55MHz; cards are unreliable
// much above ~20-25MHz over jumper-wired breakout pins.
constexpr uint32_t SPI_HZ = 4000000;
constexpr const char* BACKUP_PATH = "/cyd-weather-settings.json";
}

namespace location {
constexpr float DEFAULT_LATITUDE = 0.0f;
constexpr float DEFAULT_LONGITUDE = 0.0f;
constexpr const char* DEFAULT_TIMEZONE = "UTC0";  // POSIX TZ string
constexpr float DEFAULT_ELEVATION = 0.0f;         // metres above sea level
constexpr float LAT_MIN = -90.0f;
constexpr float LAT_MAX = 90.0f;
constexpr float LON_MIN = -180.0f;
constexpr float LON_MAX = 180.0f;
constexpr float ELEV_MIN = -500.0f;   // below Dead Sea shoreline
constexpr float ELEV_MAX = 9000.0f;   // above Everest summit
}

// Numeric values mirror the enums in weather/WeatherUnits.h (kept as plain
// ints here, same convention as cfg::log::DEFAULT_LEVEL / LogLevel).
namespace units {
constexpr uint8_t DEFAULT_TEMP = 0;      // TempUnit::Celsius
constexpr uint8_t DEFAULT_WIND = 0;      // WindUnit::KilometersPerHour
constexpr uint8_t DEFAULT_PRESSURE = 0;  // PressureUnit::HectoPascal
constexpr uint8_t TEMP_COUNT = 2;
constexpr uint8_t WIND_COUNT = 4;
constexpr uint8_t PRESSURE_COUNT = 3;
constexpr uint8_t DEFAULT_WEATHER_PROVIDER = 0;  // WeatherProvider::OpenMeteoAuto
constexpr uint8_t WEATHER_PROVIDER_COUNT = 20;
}

// Numeric value mirrors AppTheme in ui/Theme.h.
namespace theme {
constexpr uint8_t DEFAULT_THEME = 0;  // AppTheme::Dark
constexpr uint8_t COUNT = 10;
}

// Refresh cadence for WeatherService's Open-Meteo fetch (see src/weather/).
// One request returns both current conditions and the hourly forecast (the
// Hourly Forecast page's "refresh every 30 minutes" is comfortably satisfied
// by this shared 15-minute cadence — there's no separate hourly timer/request).
namespace weather {
constexpr uint32_t DEFAULT_REFRESH_S = 900;  // 15 minutes
constexpr uint32_t REFRESH_MIN_S = 60;
constexpr uint32_t REFRESH_MAX_S = 86400;
// 5 days * 24h — covers every hour of every day the 5 Day Forecast page can
// show, worst case (see pages/PageDayHourly.h, reached by tapping a day
// tile there). The Hourly Forecast page (Page 3) is unaffected: it already
// caps itself to HOURLY_DISPLAY_COUNT regardless of how many hours were
// fetched, so this alone doesn't change that page's behaviour.
constexpr uint8_t HOURLY_FETCH_COUNT = 120;
constexpr uint8_t HOURLY_DISPLAY_COUNT = 12;  // hours shown on the Hourly Forecast page
constexpr uint8_t DAILY_FETCH_COUNT = 5;      // days requested from Open-Meteo (5 Day Forecast page)
}

// Local weather history log (storage/WeatherHistoryManager.h) — a flat CSV
// file on LittleFS, capped by reading count rather than by date math (no
// on-device clock to compute "30 days ago" from). At the 15-minute logging
// floor below, MAX_READINGS works out to almost exactly 30 days; the file
// still comfortably covers the 24h/7d graphs the same task asks for, since
// those just read the newest slice of the same log.
namespace history {
constexpr uint32_t MIN_INTERVAL_S = 900;   // 15 minutes — matches the task's logging floor
constexpr size_t MAX_READINGS = 2880;      // 30 days at one reading per MIN_INTERVAL_S
// When a write would exceed MAX_READINGS, trim back down to this many
// (dropping the oldest) rather than trimming to exactly MAX_READINGS every
// single time — one rewrite then buys ~3 days before the next is needed.
constexpr size_t TRIM_TARGET = (MAX_READINGS * 9) / 10;
constexpr const char* FILE_PATH = "/history.csv";
constexpr const char* TEMP_FILE_PATH = "/history.tmp";
// Tiny sidecar (reading count + last reading only) so a reboot doesn't need
// to re-scan the whole log to recover state — see WeatherHistoryManager::begin().
constexpr const char* META_PATH = "/history.meta";
}

// Weather Alert thresholds (weather/WeatherAlertManager.h) — defaults only.
// Persisted as AppSettings::alert* (storage/SettingsManager.h, schema 5) so
// they're already in place for a future WebUI configuration page; nothing
// reads/writes them over HTTP yet.
namespace alerts {
constexpr bool DEFAULT_ENABLED = true;
constexpr float DEFAULT_WIND_WARNING_KMH = 40.0f;       // High Wind
constexpr float DEFAULT_WIND_DANGER_KMH = 65.0f;        // Very High Wind
constexpr float DEFAULT_RAIN_PROB_WARNING_PCT = 60.0f;  // Heavy Rain (paired with a heavy-rain code)
constexpr float DEFAULT_HEAT_WARNING_C = 32.0f;         // feels-like
constexpr float DEFAULT_COLD_WARNING_C = 0.0f;
constexpr float DEFAULT_UV_WARNING_INDEX = 8.0f;
constexpr float DEFAULT_PRESSURE_CHANGE_HPA = 3.0f;  // over ~3h, either direction
// setSetting() clamp bounds.
constexpr float WIND_MIN_KMH = 5.0f;
constexpr float WIND_MAX_KMH = 200.0f;
constexpr float RAIN_PROB_MIN_PCT = 10.0f;
constexpr float RAIN_PROB_MAX_PCT = 100.0f;
constexpr float TEMP_MIN_C = -40.0f;
constexpr float TEMP_MAX_C = 55.0f;
constexpr float UV_MIN_INDEX = 1.0f;
constexpr float UV_MAX_INDEX = 15.0f;
constexpr float PRESSURE_CHANGE_MIN_HPA = 0.5f;
constexpr float PRESSURE_CHANGE_MAX_HPA = 20.0f;
}

namespace defaults {
constexpr const char* HOSTNAME = "cyd-weather";
constexpr uint8_t BRIGHTNESS = 220;
constexpr uint8_t DIM_BRIGHTNESS = 15;
constexpr uint16_t DIM_TIMEOUT_S = 120;
// Display-compatibility defaults (display/DisplayConfig.h). Chosen to match
// this board's ST7789 panel and its TFT_INVERSION_OFF build flag
// (platformio.ini) so first boot looks correct without needing the web
// portal. COLOR_ORDER (BGR) is carried over from this project's original
// ILI9341_2 config and not yet re-verified against the ST7789 driver —
// adjust live via the portal if colours look off; it's not a build-time
// flag, so no reflash is needed either way.
constexpr bool DISPLAY_FLIPPED = true;   // true = display::ROTATION_FLIPPED (USB-left, 180°)
constexpr uint8_t COLOR_ORDER = 1;       // ColorOrder::BGR
// Board-specific first-boot orientation (the env in platformio.ini defines
// BOARD_ILI9341 or BOARD_ST7789). The ST7789 panel comes out horizontally
// mirrored with the flip above; the ILI9341 does not. Inversion follows the
// env's TFT_INVERSION_ON flag. All of these stay adjustable in the web portal.
#if defined(BOARD_ILI9341) || defined(BOARD_CYD35)
constexpr bool MIRROR_H = false;
#else
constexpr bool MIRROR_H = true;
#endif
constexpr bool MIRROR_V = false;
#if defined(TFT_INVERSION_ON)
constexpr bool INVERT_COLORS = true;
#else
constexpr bool INVERT_COLORS = false;
#endif
constexpr bool ROTATE_90 = false;       // MADCTL row/col swap, on top of the flip above
constexpr bool SWAP_NAVIGATION = false;  // false = swipe-left advances (PageManager::next())
constexpr bool HOUR12 = false;  // false = 24-hour clock (existing behaviour, kept as the default)
}

// SNTP time sync (network/TimeManager.h) — gives the device a real,
// DST-aware wall clock (via AppSettings::timezone) instead of the
// weather-fetch-anchored estimate every clock display used before this
// existed. Synced on WiFi connect and every RESYNC_INTERVAL_S after that.
namespace ntp {
constexpr const char* DEFAULT_SERVER = "pool.ntp.org";
constexpr uint32_t RESYNC_INTERVAL_S = 86400;  // 24 hours
constexpr uint32_t SYNC_TIMEOUT_MS = 15000;
}

}  // namespace cfg
