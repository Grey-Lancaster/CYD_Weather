#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "config/AppConfig.h"

// Persisted device settings, stored in NVS as one versioned blob.
//
// To evolve the schema:
//  1. Append new fields at the end of AppSettings (never reorder/resize
//     existing fields — that would break the migration path below).
//  2. Bump cfg::storage::SETTINGS_SCHEMA.
//  3. In SettingsManager.cpp, freeze the *previous* layout as AppSettingsVN
//     (copy AppSettings verbatim before you touch it) and add a migration
//     branch in begin() that reads the old layout and copies its fields
//     forward, defaulting only the new ones. See the V1 handling for the
//     pattern. This is what lets existing installs pick up new firmware
//     without losing WiFi credentials or other prior settings.
struct AppSettings {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;      // active backlight level
  uint8_t dimBrightness;   // backlight level when auto-dimmed
  uint16_t dimTimeoutS;    // 0 = never dim
  uint8_t logLevel;        // LogLevel as integer
  uint16_t touchXMin;      // XPT2046 raw calibration
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  // --- schema 2 additions ---
  char locationName[40];   // free-text label shown in the UI
  float latitude;
  float longitude;
  char timezone[48];       // POSIX TZ string, e.g. "UTC0" or "PST8PDT,M3.2.0,M11.1.0"
  uint8_t tempUnit;        // TempUnit (weather/WeatherUnits.h) as integer
  uint8_t windUnit;        // WindUnit as integer
  uint8_t pressureUnit;    // PressureUnit as integer
  uint8_t theme;           // AppTheme (ui/Theme.h) as integer
  uint8_t autoPageRotation;  // 0/1 — cycle rotatable pages while idle
  uint16_t pageDurationS;    // seconds per page when auto-rotating
  uint32_t weatherRefreshS;  // seconds between weather refreshes (future use)
  // --- schema 3 additions ---
  float elevation;              // metres above sea level
  char firmwareVersion[16];     // FW_VERSION as of the last save (upgrade detection)
  uint8_t firstBootCompleted;   // 0/1 — cleared until the initial setup flow finishes
  // --- schema 4 additions ---
  // Panel-compatibility settings (display/DisplayConfig.h) — different
  // ESP32-2432S028R board revisions need different values here. The UI is
  // landscape-only, so orientation is just a 180° flip, not a 4-way rotation.
  uint8_t displayFlipped;   // 0 = landscape USB-right, 1 = landscape USB-left (180°)
  uint8_t colorOrder;       // ColorOrder (display/DisplayConfig.h) as integer
  uint8_t mirrorH;          // 0/1 — horizontal mirror
  uint8_t mirrorV;          // 0/1 — vertical mirror
  uint8_t invertColors;     // 0/1 — panel colour inversion
  // --- schema 5 additions ---
  // Weather Alert thresholds (see weather/WeatherAlertManager.h). Prepared
  // for future WebUI configuration — reachable via getSetting()/setSetting()
  // and the serial console already, just not yet surfaced in WebPortal.
  uint8_t alertsEnabled;          // 0/1 — master on/off
  float alertWindWarningKmh;      // High Wind
  float alertWindDangerKmh;       // Very High Wind
  float alertRainProbWarningPct;  // Heavy Rain (paired with a heavy-rain weather code)
  float alertHeatWarningC;        // Heat Warning (feels-like temperature)
  float alertColdWarningC;        // Cold Warning
  float alertUvWarningIndex;      // High UV
  float alertPressureChangeHpa;   // Rapid Pressure Drop/Rise, over ~3h
  // --- schema 6 additions ---
  uint8_t weatherProvider;  // WeatherProvider (weather/WeatherUnits.h) as integer
  // --- schema 7 additions ---
  char ntpServer[48];  // SNTP server hostname (network/TimeManager.h), e.g. "pool.ntp.org"
  // --- schema 8 additions ---
  // Originally sized for a short opaque key like the other providers' — too
  // small: confirmed live that Met Office's own key is a ~1660-character
  // self-contained JWT, which silently truncated at the old 64-byte size
  // (both here and in WebAssets.h's matching maxlength), producing a
  // corrupt-signature "Invalid Credentials" from their API that looked
  // identical to a genuinely bad key. Resized in schema 11 below.
  char metOfficeApiKey[2048];  // Met Office Weather DataHub "Global Spot" API key (weather/WeatherUnits.h)
  // --- schema 9 additions ---
  uint16_t hourlyShiftS;  // seconds between window shifts on the Hourly Forecast page (pages/HourlyForecastPage.cpp)
  // --- schema 10 additions ---
  char pirateWeatherApiKey[64];  // Pirate Weather (pirateweather.net) API key (weather/WeatherUnits.h)
  // --- schema 11: metOfficeApiKey resized from 64 to 2048 bytes in place (see comment above) ---
  // --- schema 12 additions ---
  // Two more panel/touch-controller-compatibility knobs, alongside schema 4's
  // displayFlipped/colorOrder/mirrorH/mirrorV/invertColors (display/DisplayConfig.h).
  uint8_t rotate90;        // 0/1 — MADCTL row/col swap (display/DisplayConfig.h), for board
                           // variants that boot into a portrait, cropped image
  uint8_t swapNavigation;  // 0/1 — reverses which swipe direction advances vs. goes back
                           // (main.cpp's touchManager.onSwipe wiring), for touch controller
                           // variants that report X left-right backwards
};

// Owns the one persisted settings blob for the device. Reached via
// SettingsManager::instance() rather than a global variable — any module can
// call SettingsManager::instance().get() without needing a reference threaded
// through its constructor. Modules that mutate settings on every call (e.g.
// WebPortal) still take a SettingsManager& via constructor injection for
// testability; both styles resolve to the same single instance.
class SettingsManager {
 public:
  static SettingsManager& instance();

  SettingsManager(const SettingsManager&) = delete;
  SettingsManager& operator=(const SettingsManager&) = delete;

  // Loads settings from NVS, migrating forward from any recognized older
  // schema (existing fields preserved, new ones defaulted). Falls back to
  // compile-time defaults only if the stored blob matches no known schema.
  // Returns false only if NVS itself is unusable (settings then live in RAM
  // and won't persist).
  bool begin();
  bool save();
  bool factoryReset();

  // Named aliases for begin()/save()/factoryReset(), matching the naming
  // used by the serial test console (storage/SettingsConsole.h).
  bool loadSettings() { return begin(); }
  bool saveSettings() { return save(); }
  bool resetSettings() { return factoryReset(); }

  // Dumps every stored value to Serial (password masked). Intended for the
  // "settings" serial command, but safe to call any time.
  void printSettings() const;

  // Generic string-keyed access, for the serial console and anything else
  // that wants to address a field by name instead of the struct member.
  // Keys match the AppSettings field names. setSetting() clamps to the same
  // bounds as the web API and updates the in-memory copy only — call save()
  // (or saveSettings()) to persist. Returns "" / false for an unknown key.
  String getSetting(const String& key) const;
  bool setSetting(const String& key, const String& value);

  AppSettings& get() { return _settings; }
  const AppSettings& get() const { return _settings; }

 private:
  SettingsManager() = default;

  void applyDefaults();

  Preferences _prefs;
  AppSettings _settings = {};
  bool _nvsOk = false;
};
