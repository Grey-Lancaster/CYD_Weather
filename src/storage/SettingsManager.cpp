#include "storage/SettingsManager.h"

#include "config/Version.h"
#include "utils/Log.h"

static const char* TAG = "SET";

static uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float clampF(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Frozen snapshot of the schema-1 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 1
// actually wrote.
struct AppSettingsV1 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
};

static void migrateFromV1(const AppSettingsV1& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  // Reaching this path means a device with a schema-1 install already
  // completed setup at least once.
  out.firstBootCompleted = 1;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
}

// Frozen snapshot of the schema-2 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 2
// actually wrote.
struct AppSettingsV2 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
};

static void migrateFromV2(const AppSettingsV2& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  // schema 2 never had elevation; leave it at the default applyDefaults()
  // already filled in.
  out.firstBootCompleted = 1;  // an existing schema-2 install already booted
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
}

// Frozen snapshot of the schema-3 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 3
// actually wrote.
struct AppSettingsV3 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
};

static void migrateFromV3(const AppSettingsV3& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  // schema 3 never had the display-compatibility fields; leave them at the
  // defaults applyDefaults() already filled in.
}

// Frozen snapshot of the schema-4 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 4
// actually wrote.
struct AppSettingsV4 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
};

static void migrateFromV4(const AppSettingsV4& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  // schema 4 never had alert thresholds; leave them at the defaults
  // applyDefaults() already filled in.
}

// Frozen snapshot of the schema-5 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 5
// actually wrote.
struct AppSettingsV5 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
};

static void migrateFromV5(const AppSettingsV5& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  // schema 5 never had a weather provider choice; leave it at the default
  // applyDefaults() already filled in.
}

// Frozen snapshot of the schema-6 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 6
// actually wrote.
struct AppSettingsV6 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
};

static void migrateFromV6(const AppSettingsV6& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  // schema 6 never had an NTP server choice; leave it at the default
  // applyDefaults() already filled in.
}

// Frozen snapshot of the schema-7 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 7
// actually wrote.
struct AppSettingsV7 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
  char ntpServer[48];
};

static void migrateFromV7(const AppSettingsV7& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  memcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
  // schema 7 never had a Met Office DataHub API key; leave it at the default
  // applyDefaults() already filled in (empty string).
}

// Frozen snapshot of the schema-8 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 8
// actually wrote.
struct AppSettingsV8 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
  char ntpServer[48];
  char metOfficeApiKey[64];
};

static void migrateFromV8(const AppSettingsV8& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  memcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
  // strlcpy, not memcpy: old.metOfficeApiKey (schema 8's size, 64 bytes) is
  // smaller than out.metOfficeApiKey (now 2048, schema 11) — a memcpy sized
  // off the destination would read past the end of `old`.
  strlcpy(out.metOfficeApiKey, old.metOfficeApiKey, sizeof(out.metOfficeApiKey));
  // schema 8 never had an hourly-shift setting; leave it at the default
  // applyDefaults() already filled in.
}

// Frozen snapshot of the schema-9 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 9
// actually wrote.
struct AppSettingsV9 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
  char ntpServer[48];
  char metOfficeApiKey[64];
  uint16_t hourlyShiftS;
};

static void migrateFromV9(const AppSettingsV9& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  memcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
  // strlcpy, not memcpy — same reasoning as migrateFromV8 above:
  // old.metOfficeApiKey (schema 9's size, 64 bytes) is smaller than
  // out.metOfficeApiKey (now 2048, schema 11).
  strlcpy(out.metOfficeApiKey, old.metOfficeApiKey, sizeof(out.metOfficeApiKey));
  out.hourlyShiftS = old.hourlyShiftS;
  // schema 9 never had a Pirate Weather API key; leave it at the default
  // applyDefaults() already filled in (empty string).
}

// Frozen snapshot of the schema-10 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 10
// actually wrote. Note metOfficeApiKey's size here (64) is deliberately the
// old, too-small size — see schema 11's comment on AppSettings.
struct AppSettingsV10 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
  char ntpServer[48];
  char metOfficeApiKey[64];
  uint16_t hourlyShiftS;
  char pirateWeatherApiKey[64];
};

static void migrateFromV10(const AppSettingsV10& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  memcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
  // Old value was truncated at 64 bytes anyway (that was the bug) — not
  // worth preserving a corrupt key, so this carries it forward as-is into
  // the new, larger field and lets the user re-paste a full one.
  strlcpy(out.metOfficeApiKey, old.metOfficeApiKey, sizeof(out.metOfficeApiKey));
  out.hourlyShiftS = old.hourlyShiftS;
  memcpy(out.pirateWeatherApiKey, old.pirateWeatherApiKey, sizeof(out.pirateWeatherApiKey));
}

// Frozen snapshot of the schema-11 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 11
// actually wrote. Same as AppSettingsV10 except metOfficeApiKey's resized
// size (see schema 11's comment on AppSettings).
struct AppSettingsV11 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
  char ntpServer[48];
  char metOfficeApiKey[2048];
  uint16_t hourlyShiftS;
  char pirateWeatherApiKey[64];
};

static void migrateFromV11(const AppSettingsV11& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  memcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
  memcpy(out.metOfficeApiKey, old.metOfficeApiKey, sizeof(out.metOfficeApiKey));
  out.hourlyShiftS = old.hourlyShiftS;
  memcpy(out.pirateWeatherApiKey, old.pirateWeatherApiKey, sizeof(out.pirateWeatherApiKey));
  // schema 11 never had the rotate90/swapNavigation knobs; leave them at the
  // defaults applyDefaults() already filled in.
}

// Frozen snapshot of the schema-12 AppSettings layout, kept only so an
// existing install's NVS blob can be read and migrated forward. Never edit
// this struct — it must stay byte-for-byte identical to what schema 12
// actually wrote.
struct AppSettingsV12 {
  uint32_t magic;
  uint16_t schemaVersion;
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t brightness;
  uint8_t dimBrightness;
  uint16_t dimTimeoutS;
  uint8_t logLevel;
  uint16_t touchXMin;
  uint16_t touchXMax;
  uint16_t touchYMin;
  uint16_t touchYMax;
  char locationName[40];
  float latitude;
  float longitude;
  char timezone[48];
  uint8_t tempUnit;
  uint8_t windUnit;
  uint8_t pressureUnit;
  uint8_t theme;
  uint8_t autoPageRotation;
  uint16_t pageDurationS;
  uint32_t weatherRefreshS;
  float elevation;
  char firmwareVersion[16];
  uint8_t firstBootCompleted;
  uint8_t displayFlipped;
  uint8_t colorOrder;
  uint8_t mirrorH;
  uint8_t mirrorV;
  uint8_t invertColors;
  uint8_t alertsEnabled;
  float alertWindWarningKmh;
  float alertWindDangerKmh;
  float alertRainProbWarningPct;
  float alertHeatWarningC;
  float alertColdWarningC;
  float alertUvWarningIndex;
  float alertPressureChangeHpa;
  uint8_t weatherProvider;
  char ntpServer[48];
  char metOfficeApiKey[2048];
  uint16_t hourlyShiftS;
  char pirateWeatherApiKey[64];
  uint8_t rotate90;
  uint8_t swapNavigation;
};

static void migrateFromV12(const AppSettingsV12& old, AppSettings& out) {
  out.magic = cfg::storage::SETTINGS_MAGIC;
  out.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  memcpy(out.wifiSsid, old.wifiSsid, sizeof(out.wifiSsid));
  memcpy(out.wifiPass, old.wifiPass, sizeof(out.wifiPass));
  memcpy(out.hostname, old.hostname, sizeof(out.hostname));
  out.brightness = old.brightness;
  out.dimBrightness = old.dimBrightness;
  out.dimTimeoutS = old.dimTimeoutS;
  out.logLevel = old.logLevel;
  out.touchXMin = old.touchXMin;
  out.touchXMax = old.touchXMax;
  out.touchYMin = old.touchYMin;
  out.touchYMax = old.touchYMax;
  memcpy(out.locationName, old.locationName, sizeof(out.locationName));
  out.latitude = old.latitude;
  out.longitude = old.longitude;
  memcpy(out.timezone, old.timezone, sizeof(out.timezone));
  out.tempUnit = old.tempUnit;
  out.windUnit = old.windUnit;
  out.pressureUnit = old.pressureUnit;
  out.theme = old.theme;
  out.autoPageRotation = old.autoPageRotation;
  out.pageDurationS = old.pageDurationS;
  out.weatherRefreshS = old.weatherRefreshS;
  out.elevation = old.elevation;
  strlcpy(out.firmwareVersion, FW_VERSION, sizeof(out.firmwareVersion));
  out.firstBootCompleted = old.firstBootCompleted;
  out.displayFlipped = old.displayFlipped;
  out.colorOrder = old.colorOrder;
  out.mirrorH = old.mirrorH;
  out.mirrorV = old.mirrorV;
  out.invertColors = old.invertColors;
  out.alertsEnabled = old.alertsEnabled;
  out.alertWindWarningKmh = old.alertWindWarningKmh;
  out.alertWindDangerKmh = old.alertWindDangerKmh;
  out.alertRainProbWarningPct = old.alertRainProbWarningPct;
  out.alertHeatWarningC = old.alertHeatWarningC;
  out.alertColdWarningC = old.alertColdWarningC;
  out.alertUvWarningIndex = old.alertUvWarningIndex;
  out.alertPressureChangeHpa = old.alertPressureChangeHpa;
  out.weatherProvider = old.weatherProvider;
  memcpy(out.ntpServer, old.ntpServer, sizeof(out.ntpServer));
  memcpy(out.metOfficeApiKey, old.metOfficeApiKey, sizeof(out.metOfficeApiKey));
  out.hourlyShiftS = old.hourlyShiftS;
  memcpy(out.pirateWeatherApiKey, old.pirateWeatherApiKey, sizeof(out.pirateWeatherApiKey));
  out.rotate90 = old.rotate90;
  out.swapNavigation = old.swapNavigation;
  // schema 12 never had hour12; leave it at the default applyDefaults()
  // already filled in.
}

bool SettingsManager::begin() {
  _nvsOk = _prefs.begin(cfg::storage::NVS_NAMESPACE, false);
  if (!_nvsOk) {
    LOGE(TAG, "NVS open failed, running on defaults (changes will not persist)");
    applyDefaults();
    return false;
  }

  const size_t got = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &_settings, sizeof(_settings));
  if (got == sizeof(_settings) && _settings.magic == cfg::storage::SETTINGS_MAGIC &&
      _settings.schemaVersion == cfg::storage::SETTINGS_SCHEMA) {
    LOGI(TAG, "settings loaded (schema %u)", (unsigned)_settings.schemaVersion);
    return true;
  }

  // static, not a plain local: AppSettingsV12 carries the 2048-byte
  // metOfficeApiKey field, and stacking it on top of AppSettingsV11's
  // identically-sized copy (same function, both declared unconditionally on
  // entry) overflowed loopTask's stack on real hardware — confirmed via a
  // boot-loop with "stack overflow in task loopTask" right after this
  // function's first log line. Moves it to .bss instead; begin() only runs
  // once at boot, so there's no stale-data risk from reusing a static here.
  static AppSettingsV12 v12;
  memset(&v12, 0, sizeof(v12));
  const size_t gotV12 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v12, sizeof(v12));
  if (gotV12 == sizeof(v12) && v12.magic == cfg::storage::SETTINGS_MAGIC && v12.schemaVersion == 12) {
    applyDefaults();  // fills every field, including ones schema 12 never had
    migrateFromV12(v12, _settings);
    LOGW(TAG, "migrated settings from schema 12 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV11 v11 = {};
  const size_t gotV11 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v11, sizeof(v11));
  if (gotV11 == sizeof(v11) && v11.magic == cfg::storage::SETTINGS_MAGIC && v11.schemaVersion == 11) {
    applyDefaults();  // fills every field, including ones schema 11 never had
    migrateFromV11(v11, _settings);
    LOGW(TAG, "migrated settings from schema 11 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV10 v10 = {};
  const size_t gotV10 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v10, sizeof(v10));
  if (gotV10 == sizeof(v10) && v10.magic == cfg::storage::SETTINGS_MAGIC && v10.schemaVersion == 10) {
    applyDefaults();  // fills every field, including ones schema 10 never had
    migrateFromV10(v10, _settings);
    LOGW(TAG, "migrated settings from schema 10 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV9 v9 = {};
  const size_t gotV9 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v9, sizeof(v9));
  if (gotV9 == sizeof(v9) && v9.magic == cfg::storage::SETTINGS_MAGIC && v9.schemaVersion == 9) {
    applyDefaults();  // fills every field, including ones schema 9 never had
    migrateFromV9(v9, _settings);
    LOGW(TAG, "migrated settings from schema 9 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV8 v8 = {};
  const size_t gotV8 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v8, sizeof(v8));
  if (gotV8 == sizeof(v8) && v8.magic == cfg::storage::SETTINGS_MAGIC && v8.schemaVersion == 8) {
    applyDefaults();  // fills every field, including ones schema 8 never had
    migrateFromV8(v8, _settings);
    LOGW(TAG, "migrated settings from schema 8 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV7 v7 = {};
  const size_t gotV7 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v7, sizeof(v7));
  if (gotV7 == sizeof(v7) && v7.magic == cfg::storage::SETTINGS_MAGIC && v7.schemaVersion == 7) {
    applyDefaults();  // fills every field, including ones schema 7 never had
    migrateFromV7(v7, _settings);
    LOGW(TAG, "migrated settings from schema 7 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV6 v6 = {};
  const size_t gotV6 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v6, sizeof(v6));
  if (gotV6 == sizeof(v6) && v6.magic == cfg::storage::SETTINGS_MAGIC && v6.schemaVersion == 6) {
    applyDefaults();  // fills every field, including ones schema 6 never had
    migrateFromV6(v6, _settings);
    LOGW(TAG, "migrated settings from schema 6 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV5 v5 = {};
  const size_t gotV5 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v5, sizeof(v5));
  if (gotV5 == sizeof(v5) && v5.magic == cfg::storage::SETTINGS_MAGIC && v5.schemaVersion == 5) {
    applyDefaults();  // fills every field, including ones schema 5 never had
    migrateFromV5(v5, _settings);
    LOGW(TAG, "migrated settings from schema 5 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV4 v4 = {};
  const size_t gotV4 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v4, sizeof(v4));
  if (gotV4 == sizeof(v4) && v4.magic == cfg::storage::SETTINGS_MAGIC && v4.schemaVersion == 4) {
    applyDefaults();  // fills every field, including ones schema 4 never had
    migrateFromV4(v4, _settings);
    LOGW(TAG, "migrated settings from schema 4 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV3 v3 = {};
  const size_t gotV3 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v3, sizeof(v3));
  if (gotV3 == sizeof(v3) && v3.magic == cfg::storage::SETTINGS_MAGIC && v3.schemaVersion == 3) {
    applyDefaults();  // fills every field, including ones schema 3 never had
    migrateFromV3(v3, _settings);
    LOGW(TAG, "migrated settings from schema 3 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV2 v2 = {};
  const size_t gotV2 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v2, sizeof(v2));
  if (gotV2 == sizeof(v2) && v2.magic == cfg::storage::SETTINGS_MAGIC && v2.schemaVersion == 2) {
    applyDefaults();  // fills every field, including ones schema 2 never had
    migrateFromV2(v2, _settings);
    LOGW(TAG, "migrated settings from schema 2 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  AppSettingsV1 v1 = {};
  const size_t gotV1 = _prefs.getBytes(cfg::storage::SETTINGS_KEY, &v1, sizeof(v1));
  if (gotV1 == sizeof(v1) && v1.magic == cfg::storage::SETTINGS_MAGIC && v1.schemaVersion == 1) {
    applyDefaults();  // fills every field, including ones schema 1 never had
    migrateFromV1(v1, _settings);
    LOGW(TAG, "migrated settings from schema 1 to %u", (unsigned)cfg::storage::SETTINGS_SCHEMA);
    save();
    return true;
  }

  LOGW(TAG, "stored settings unrecognized (len=%u), applying defaults", (unsigned)got);
  applyDefaults();
  save();
  return true;
}

bool SettingsManager::save() {
  if (!_nvsOk) {
    LOGW(TAG, "NVS unavailable, settings not persisted");
    return false;
  }
  const size_t written = _prefs.putBytes(cfg::storage::SETTINGS_KEY, &_settings, sizeof(_settings));
  if (written != sizeof(_settings)) {
    LOGE(TAG, "save failed (wrote %u of %u bytes)", (unsigned)written, (unsigned)sizeof(_settings));
    return false;
  }
  LOGI(TAG, "settings saved");
  return true;
}

bool SettingsManager::factoryReset() {
  if (_nvsOk) _prefs.clear();
  applyDefaults();
  const bool ok = save();
  LOGW(TAG, "factory reset complete");
  return ok;
}

void SettingsManager::applyDefaults() {
  memset(&_settings, 0, sizeof(_settings));
  _settings.magic = cfg::storage::SETTINGS_MAGIC;
  _settings.schemaVersion = cfg::storage::SETTINGS_SCHEMA;
  strlcpy(_settings.hostname, cfg::defaults::HOSTNAME, sizeof(_settings.hostname));
  strlcpy(_settings.wifiSsid, cfg::net::DEFAULT_WIFI_SSID, sizeof(_settings.wifiSsid));
  strlcpy(_settings.wifiPass, cfg::net::DEFAULT_WIFI_PASSWORD, sizeof(_settings.wifiPass));
  _settings.brightness = cfg::defaults::BRIGHTNESS;
  _settings.dimBrightness = cfg::defaults::DIM_BRIGHTNESS;
  _settings.dimTimeoutS = cfg::defaults::DIM_TIMEOUT_S;
  _settings.logLevel = cfg::log::DEFAULT_LEVEL;
  _settings.touchXMin = cfg::touch::RAW_X_MIN;
  _settings.touchXMax = cfg::touch::RAW_X_MAX;
  _settings.touchYMin = cfg::touch::RAW_Y_MIN;
  _settings.touchYMax = cfg::touch::RAW_Y_MAX;
  // locationName left empty ("unset")
  _settings.latitude = cfg::location::DEFAULT_LATITUDE;
  _settings.longitude = cfg::location::DEFAULT_LONGITUDE;
  strlcpy(_settings.timezone, cfg::location::DEFAULT_TIMEZONE, sizeof(_settings.timezone));
  _settings.tempUnit = cfg::units::DEFAULT_TEMP;
  _settings.windUnit = cfg::units::DEFAULT_WIND;
  _settings.pressureUnit = cfg::units::DEFAULT_PRESSURE;
  _settings.theme = cfg::theme::DEFAULT_THEME;
  _settings.autoPageRotation = cfg::ui::DEFAULT_AUTO_ROTATE ? 1 : 0;
  _settings.pageDurationS = cfg::ui::DEFAULT_PAGE_DURATION_S;
  _settings.hourlyShiftS = cfg::ui::DEFAULT_HOURLY_SHIFT_S;
  _settings.weatherRefreshS = cfg::weather::DEFAULT_REFRESH_S;
  _settings.elevation = cfg::location::DEFAULT_ELEVATION;
  strlcpy(_settings.firmwareVersion, FW_VERSION, sizeof(_settings.firmwareVersion));
  _settings.firstBootCompleted = 0;
  _settings.displayFlipped = cfg::defaults::DISPLAY_FLIPPED ? 1 : 0;
  _settings.colorOrder = cfg::defaults::COLOR_ORDER;
  _settings.mirrorH = cfg::defaults::MIRROR_H ? 1 : 0;
  _settings.mirrorV = cfg::defaults::MIRROR_V ? 1 : 0;
  _settings.invertColors = cfg::defaults::INVERT_COLORS ? 1 : 0;
  _settings.rotate90 = cfg::defaults::ROTATE_90 ? 1 : 0;
  _settings.swapNavigation = cfg::defaults::SWAP_NAVIGATION ? 1 : 0;
  _settings.hour12 = cfg::defaults::HOUR12 ? 1 : 0;
  _settings.alertsEnabled = cfg::alerts::DEFAULT_ENABLED ? 1 : 0;
  _settings.alertWindWarningKmh = cfg::alerts::DEFAULT_WIND_WARNING_KMH;
  _settings.alertWindDangerKmh = cfg::alerts::DEFAULT_WIND_DANGER_KMH;
  _settings.alertRainProbWarningPct = cfg::alerts::DEFAULT_RAIN_PROB_WARNING_PCT;
  _settings.alertHeatWarningC = cfg::alerts::DEFAULT_HEAT_WARNING_C;
  _settings.alertColdWarningC = cfg::alerts::DEFAULT_COLD_WARNING_C;
  _settings.alertUvWarningIndex = cfg::alerts::DEFAULT_UV_WARNING_INDEX;
  _settings.alertPressureChangeHpa = cfg::alerts::DEFAULT_PRESSURE_CHANGE_HPA;
  _settings.weatherProvider = cfg::units::DEFAULT_WEATHER_PROVIDER;
  strlcpy(_settings.ntpServer, cfg::ntp::DEFAULT_SERVER, sizeof(_settings.ntpServer));
  // metOfficeApiKey/pirateWeatherApiKey left empty ("unset") — the memset
  // above already did this.
}

SettingsManager& SettingsManager::instance() {
  // Function-local static: zero-init before C++ runtime guarantees run,
  // constructed on first use, no static-initialization-order concerns and no
  // file-scope global variable for callers to reach into directly.
  static SettingsManager mgr;
  return mgr;
}

void SettingsManager::printSettings() const {
  const AppSettings& s = _settings;
  Serial.println(F("---- settings ----"));
  Serial.printf("  schemaVersion    = %u\r\n", (unsigned)s.schemaVersion);
  Serial.printf("  [wifi]\r\n");
  Serial.printf("  wifiSsid         = %s\r\n", s.wifiSsid);
  Serial.printf("  wifiPass         = %s\r\n", s.wifiPass[0] != '\0' ? "********" : "");
  Serial.printf("  [location]\r\n");
  Serial.printf("  locationName     = %s\r\n", s.locationName);
  Serial.printf("  latitude         = %.5f\r\n", s.latitude);
  Serial.printf("  longitude        = %.5f\r\n", s.longitude);
  Serial.printf("  elevation        = %.1f m\r\n", s.elevation);
  Serial.printf("  timezone         = %s\r\n", s.timezone);
  Serial.printf("  [display]\r\n");
  Serial.printf("  brightness       = %u\r\n", (unsigned)s.brightness);
  Serial.printf("  dimBrightness    = %u\r\n", (unsigned)s.dimBrightness);
  Serial.printf("  dimTimeoutS      = %u\r\n", (unsigned)s.dimTimeoutS);
  Serial.printf("  theme            = %u\r\n", (unsigned)s.theme);
  Serial.printf("  displayFlipped   = %s\r\n", s.displayFlipped ? "true" : "false");
  Serial.printf("  colorOrder       = %u (%s)\r\n", (unsigned)s.colorOrder,
                s.colorOrder ? "BGR" : "RGB");
  Serial.printf("  mirrorH          = %s\r\n", s.mirrorH ? "true" : "false");
  Serial.printf("  mirrorV          = %s\r\n", s.mirrorV ? "true" : "false");
  Serial.printf("  invertColors     = %s\r\n", s.invertColors ? "true" : "false");
  Serial.printf("  rotate90         = %s\r\n", s.rotate90 ? "true" : "false");
  Serial.printf("  swapNavigation   = %s\r\n", s.swapNavigation ? "true" : "false");
  Serial.printf("  autoPageRotation = %s\r\n", s.autoPageRotation ? "true" : "false");
  Serial.printf("  pageDurationS    = %u\r\n", (unsigned)s.pageDurationS);
  Serial.printf("  hourlyShiftS     = %u\r\n", (unsigned)s.hourlyShiftS);
  Serial.printf("  [weather]\r\n");
  Serial.printf("  tempUnit         = %u\r\n", (unsigned)s.tempUnit);
  Serial.printf("  windUnit         = %u\r\n", (unsigned)s.windUnit);
  Serial.printf("  pressureUnit     = %u\r\n", (unsigned)s.pressureUnit);
  Serial.printf("  weatherRefreshS  = %lu\r\n", (unsigned long)s.weatherRefreshS);
  Serial.printf("  weatherProvider  = %u\r\n", (unsigned)s.weatherProvider);
  Serial.printf("  [system]\r\n");
  Serial.printf("  hostname         = %s\r\n", s.hostname);
  Serial.printf("  ntpServer        = %s\r\n", s.ntpServer);
  Serial.printf("  metOfficeApiKey  = %s\r\n", s.metOfficeApiKey[0] != '\0' ? "********" : "");
  Serial.printf("  pirateWeatherApiKey = %s\r\n", s.pirateWeatherApiKey[0] != '\0' ? "********" : "");
  Serial.printf("  firmwareVersion  = %s (running %s)\r\n", s.firmwareVersion, FW_VERSION);
  Serial.printf("  firstBootCompleted = %s\r\n", s.firstBootCompleted ? "true" : "false");
  Serial.printf("  logLevel         = %u\r\n", (unsigned)s.logLevel);
  Serial.printf("  [alerts]\r\n");
  Serial.printf("  alertsEnabled    = %s\r\n", s.alertsEnabled ? "true" : "false");
  Serial.printf("  alertWindWarningKmh     = %.1f\r\n", (double)s.alertWindWarningKmh);
  Serial.printf("  alertWindDangerKmh      = %.1f\r\n", (double)s.alertWindDangerKmh);
  Serial.printf("  alertRainProbWarningPct = %.0f\r\n", (double)s.alertRainProbWarningPct);
  Serial.printf("  alertHeatWarningC       = %.1f\r\n", (double)s.alertHeatWarningC);
  Serial.printf("  alertColdWarningC       = %.1f\r\n", (double)s.alertColdWarningC);
  Serial.printf("  alertUvWarningIndex     = %.1f\r\n", (double)s.alertUvWarningIndex);
  Serial.printf("  alertPressureChangeHpa  = %.1f\r\n", (double)s.alertPressureChangeHpa);
  Serial.println(F("-------------------"));
}

String SettingsManager::getSetting(const String& key) const {
  const AppSettings& s = _settings;
  if (key == "wifiSsid") return s.wifiSsid;
  if (key == "wifiPass") return s.wifiPass[0] != '\0' ? "********" : "";
  if (key == "hostname") return s.hostname;
  if (key == "brightness") return String(s.brightness);
  if (key == "dimBrightness") return String(s.dimBrightness);
  if (key == "dimTimeoutS") return String(s.dimTimeoutS);
  if (key == "logLevel") return String(s.logLevel);
  if (key == "locationName") return s.locationName;
  if (key == "latitude") return String(s.latitude, 5);
  if (key == "longitude") return String(s.longitude, 5);
  if (key == "elevation") return String(s.elevation, 1);
  if (key == "timezone") return s.timezone;
  if (key == "tempUnit") return String(s.tempUnit);
  if (key == "windUnit") return String(s.windUnit);
  if (key == "pressureUnit") return String(s.pressureUnit);
  if (key == "theme") return String(s.theme);
  if (key == "displayFlipped") return s.displayFlipped ? "true" : "false";
  if (key == "colorOrder") return String(s.colorOrder);
  if (key == "mirrorH") return s.mirrorH ? "true" : "false";
  if (key == "mirrorV") return s.mirrorV ? "true" : "false";
  if (key == "invertColors") return s.invertColors ? "true" : "false";
  if (key == "rotate90") return s.rotate90 ? "true" : "false";
  if (key == "swapNavigation") return s.swapNavigation ? "true" : "false";
  if (key == "autoPageRotation") return s.autoPageRotation ? "true" : "false";
  if (key == "pageDurationS") return String(s.pageDurationS);
  if (key == "hourlyShiftS") return String(s.hourlyShiftS);
  if (key == "weatherRefreshS") return String(s.weatherRefreshS);
  if (key == "firmwareVersion") return s.firmwareVersion;
  if (key == "firstBootCompleted") return s.firstBootCompleted ? "true" : "false";
  if (key == "alertsEnabled") return s.alertsEnabled ? "true" : "false";
  if (key == "alertWindWarningKmh") return String(s.alertWindWarningKmh, 1);
  if (key == "alertWindDangerKmh") return String(s.alertWindDangerKmh, 1);
  if (key == "alertRainProbWarningPct") return String(s.alertRainProbWarningPct, 0);
  if (key == "alertHeatWarningC") return String(s.alertHeatWarningC, 1);
  if (key == "alertColdWarningC") return String(s.alertColdWarningC, 1);
  if (key == "alertUvWarningIndex") return String(s.alertUvWarningIndex, 1);
  if (key == "alertPressureChangeHpa") return String(s.alertPressureChangeHpa, 1);
  if (key == "weatherProvider") return String(s.weatherProvider);
  if (key == "ntpServer") return s.ntpServer;
  if (key == "metOfficeApiKey") return s.metOfficeApiKey[0] != '\0' ? "********" : "";
  if (key == "pirateWeatherApiKey") return s.pirateWeatherApiKey[0] != '\0' ? "********" : "";
  LOGW(TAG, "getSetting: unknown key '%s'", key.c_str());
  return "";
}

bool SettingsManager::setSetting(const String& key, const String& value) {
  AppSettings& s = _settings;
  if (key == "wifiSsid") {
    strlcpy(s.wifiSsid, value.c_str(), sizeof(s.wifiSsid));
  } else if (key == "wifiPass") {
    strlcpy(s.wifiPass, value.c_str(), sizeof(s.wifiPass));
  } else if (key == "hostname") {
    strlcpy(s.hostname, value.c_str(), sizeof(s.hostname));
  } else if (key == "brightness") {
    s.brightness = (uint8_t)clampU32(value.toInt(), cfg::display::MIN_BRIGHTNESS, 255);
  } else if (key == "dimBrightness") {
    s.dimBrightness = (uint8_t)clampU32(value.toInt(), 0, 255);
  } else if (key == "dimTimeoutS") {
    s.dimTimeoutS = (uint16_t)clampU32(value.toInt(), 0, 65535);
  } else if (key == "logLevel") {
    s.logLevel = (uint8_t)clampU32(value.toInt(), 0, 4);
  } else if (key == "locationName") {
    strlcpy(s.locationName, value.c_str(), sizeof(s.locationName));
  } else if (key == "latitude") {
    s.latitude = clampF(value.toFloat(), cfg::location::LAT_MIN, cfg::location::LAT_MAX);
  } else if (key == "longitude") {
    s.longitude = clampF(value.toFloat(), cfg::location::LON_MIN, cfg::location::LON_MAX);
  } else if (key == "elevation") {
    s.elevation = clampF(value.toFloat(), cfg::location::ELEV_MIN, cfg::location::ELEV_MAX);
  } else if (key == "timezone") {
    strlcpy(s.timezone, value.c_str(), sizeof(s.timezone));
  } else if (key == "tempUnit") {
    s.tempUnit = (uint8_t)clampU32(value.toInt(), 0, cfg::units::TEMP_COUNT - 1);
  } else if (key == "windUnit") {
    s.windUnit = (uint8_t)clampU32(value.toInt(), 0, cfg::units::WIND_COUNT - 1);
  } else if (key == "pressureUnit") {
    s.pressureUnit = (uint8_t)clampU32(value.toInt(), 0, cfg::units::PRESSURE_COUNT - 1);
  } else if (key == "theme") {
    s.theme = (uint8_t)clampU32(value.toInt(), 0, cfg::theme::COUNT - 1);
  } else if (key == "displayFlipped") {
    s.displayFlipped = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "colorOrder") {
    s.colorOrder = (uint8_t)clampU32(value.toInt(), 0, cfg::display::COLOR_ORDER_COUNT - 1);
  } else if (key == "mirrorH") {
    s.mirrorH = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "mirrorV") {
    s.mirrorV = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "invertColors") {
    s.invertColors = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "rotate90") {
    s.rotate90 = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "swapNavigation") {
    s.swapNavigation = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "autoPageRotation") {
    s.autoPageRotation = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "pageDurationS") {
    s.pageDurationS = (uint16_t)clampU32(value.toInt(), cfg::ui::PAGE_DURATION_MIN_S,
                                         cfg::ui::PAGE_DURATION_MAX_S);
  } else if (key == "hourlyShiftS") {
    s.hourlyShiftS = (uint16_t)clampU32(value.toInt(), cfg::ui::HOURLY_SHIFT_MIN_S,
                                        cfg::ui::HOURLY_SHIFT_MAX_S);
  } else if (key == "weatherRefreshS") {
    s.weatherRefreshS = clampU32(value.toInt(), cfg::weather::REFRESH_MIN_S, cfg::weather::REFRESH_MAX_S);
  } else if (key == "firstBootCompleted") {
    s.firstBootCompleted = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "alertsEnabled") {
    s.alertsEnabled = (value == "true" || value == "1") ? 1 : 0;
  } else if (key == "alertWindWarningKmh") {
    s.alertWindWarningKmh = clampF(value.toFloat(), cfg::alerts::WIND_MIN_KMH, cfg::alerts::WIND_MAX_KMH);
  } else if (key == "alertWindDangerKmh") {
    s.alertWindDangerKmh = clampF(value.toFloat(), cfg::alerts::WIND_MIN_KMH, cfg::alerts::WIND_MAX_KMH);
  } else if (key == "alertRainProbWarningPct") {
    s.alertRainProbWarningPct =
        clampF(value.toFloat(), cfg::alerts::RAIN_PROB_MIN_PCT, cfg::alerts::RAIN_PROB_MAX_PCT);
  } else if (key == "alertHeatWarningC") {
    s.alertHeatWarningC = clampF(value.toFloat(), cfg::alerts::TEMP_MIN_C, cfg::alerts::TEMP_MAX_C);
  } else if (key == "alertColdWarningC") {
    s.alertColdWarningC = clampF(value.toFloat(), cfg::alerts::TEMP_MIN_C, cfg::alerts::TEMP_MAX_C);
  } else if (key == "alertUvWarningIndex") {
    s.alertUvWarningIndex = clampF(value.toFloat(), cfg::alerts::UV_MIN_INDEX, cfg::alerts::UV_MAX_INDEX);
  } else if (key == "alertPressureChangeHpa") {
    s.alertPressureChangeHpa = clampF(value.toFloat(), cfg::alerts::PRESSURE_CHANGE_MIN_HPA,
                                      cfg::alerts::PRESSURE_CHANGE_MAX_HPA);
  } else if (key == "weatherProvider") {
    s.weatherProvider =
        (uint8_t)clampU32(value.toInt(), 0, cfg::units::WEATHER_PROVIDER_COUNT - 1);
  } else if (key == "ntpServer") {
    strlcpy(s.ntpServer, value.c_str(), sizeof(s.ntpServer));
  } else if (key == "metOfficeApiKey") {
    strlcpy(s.metOfficeApiKey, value.c_str(), sizeof(s.metOfficeApiKey));
  } else if (key == "pirateWeatherApiKey") {
    strlcpy(s.pirateWeatherApiKey, value.c_str(), sizeof(s.pirateWeatherApiKey));
  } else {
    LOGW(TAG, "setSetting: unknown key '%s'", key.c_str());
    return false;
  }
  return true;
}
