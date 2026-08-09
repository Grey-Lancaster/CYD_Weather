#include "storage/SettingsBackup.h"

#include "config/AppConfig.h"
#include "config/Version.h"

static uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float clampF(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

namespace SettingsBackup {

void toJson(const AppSettings& s, JsonDocument& doc) {
  doc["device"] = FW_NAME;
  doc["firmwareVersion"] = FW_VERSION;
  doc["schemaVersion"] = s.schemaVersion;

  JsonObject o = doc["settings"].to<JsonObject>();
  o["hostname"] = s.hostname;
  o["ntpServer"] = s.ntpServer;
  o["brightness"] = s.brightness;
  o["dimBrightness"] = s.dimBrightness;
  o["dimTimeoutS"] = s.dimTimeoutS;
  o["logLevel"] = s.logLevel;
  o["touchXMin"] = s.touchXMin;
  o["touchXMax"] = s.touchXMax;
  o["touchYMin"] = s.touchYMin;
  o["touchYMax"] = s.touchYMax;
  o["locationName"] = s.locationName;
  o["latitude"] = s.latitude;
  o["longitude"] = s.longitude;
  o["elevation"] = s.elevation;
  o["timezone"] = s.timezone;
  o["tempUnit"] = s.tempUnit;
  o["windUnit"] = s.windUnit;
  o["pressureUnit"] = s.pressureUnit;
  o["weatherProvider"] = s.weatherProvider;
  o["metOfficeApiKey"] = s.metOfficeApiKey;
  o["pirateWeatherApiKey"] = s.pirateWeatherApiKey;
  o["theme"] = s.theme;
  o["autoPageRotation"] = s.autoPageRotation != 0;
  o["pageDurationS"] = s.pageDurationS;
  o["hourlyShiftS"] = s.hourlyShiftS;
  o["weatherRefreshS"] = s.weatherRefreshS;
  o["colorOrder"] = s.colorOrder;
  o["displayFlipped"] = s.displayFlipped != 0;
  o["mirrorH"] = s.mirrorH != 0;
  o["mirrorV"] = s.mirrorV != 0;
  o["invertColors"] = s.invertColors != 0;
  o["rotate90"] = s.rotate90 != 0;
  o["swapNavigation"] = s.swapNavigation != 0;
  o["firstBootCompleted"] = s.firstBootCompleted != 0;
  o["alertsEnabled"] = s.alertsEnabled != 0;
  o["alertWindWarningKmh"] = s.alertWindWarningKmh;
  o["alertWindDangerKmh"] = s.alertWindDangerKmh;
  o["alertRainProbWarningPct"] = s.alertRainProbWarningPct;
  o["alertHeatWarningC"] = s.alertHeatWarningC;
  o["alertColdWarningC"] = s.alertColdWarningC;
  o["alertUvWarningIndex"] = s.alertUvWarningIndex;
  o["alertPressureChangeHpa"] = s.alertPressureChangeHpa;
  // firmwareVersion itself is deliberately not included here — it's
  // stamped with whatever build is actually running (see
  // SettingsManager::applyDefaults() and every migration path), never
  // user data, so fromJson() below never touches it either.
}

bool fromJson(const JsonDocument& doc, AppSettings& s) {
  JsonObjectConst o = doc["settings"];
  if (!o["hostname"].is<const char*>()) return false;

  strlcpy(s.hostname, o["hostname"], sizeof(s.hostname));
  if (o["ntpServer"].is<const char*>()) strlcpy(s.ntpServer, o["ntpServer"], sizeof(s.ntpServer));
  if (o["brightness"].is<unsigned>())
    s.brightness = (uint8_t)clampU32(o["brightness"], cfg::display::MIN_BRIGHTNESS, 255);
  if (o["dimBrightness"].is<unsigned>())
    s.dimBrightness = (uint8_t)clampU32(o["dimBrightness"], 0, 255);
  if (o["dimTimeoutS"].is<unsigned>())
    s.dimTimeoutS = (uint16_t)clampU32(o["dimTimeoutS"], 0, 65535);
  if (o["logLevel"].is<unsigned>()) s.logLevel = (uint8_t)clampU32(o["logLevel"], 0, 4);
  if (o["touchXMin"].is<unsigned>()) s.touchXMin = (uint16_t)clampU32(o["touchXMin"], 0, 4095);
  if (o["touchXMax"].is<unsigned>()) s.touchXMax = (uint16_t)clampU32(o["touchXMax"], 0, 4095);
  if (o["touchYMin"].is<unsigned>()) s.touchYMin = (uint16_t)clampU32(o["touchYMin"], 0, 4095);
  if (o["touchYMax"].is<unsigned>()) s.touchYMax = (uint16_t)clampU32(o["touchYMax"], 0, 4095);
  if (o["locationName"].is<const char*>())
    strlcpy(s.locationName, o["locationName"], sizeof(s.locationName));
  if (!o["latitude"].isNull())
    s.latitude = clampF(o["latitude"], cfg::location::LAT_MIN, cfg::location::LAT_MAX);
  if (!o["longitude"].isNull())
    s.longitude = clampF(o["longitude"], cfg::location::LON_MIN, cfg::location::LON_MAX);
  if (!o["elevation"].isNull())
    s.elevation = clampF(o["elevation"], cfg::location::ELEV_MIN, cfg::location::ELEV_MAX);
  if (o["timezone"].is<const char*>()) strlcpy(s.timezone, o["timezone"], sizeof(s.timezone));
  if (o["tempUnit"].is<unsigned>())
    s.tempUnit = (uint8_t)clampU32(o["tempUnit"], 0, cfg::units::TEMP_COUNT - 1);
  if (o["windUnit"].is<unsigned>())
    s.windUnit = (uint8_t)clampU32(o["windUnit"], 0, cfg::units::WIND_COUNT - 1);
  if (o["pressureUnit"].is<unsigned>())
    s.pressureUnit = (uint8_t)clampU32(o["pressureUnit"], 0, cfg::units::PRESSURE_COUNT - 1);
  if (o["weatherProvider"].is<unsigned>())
    s.weatherProvider =
        (uint8_t)clampU32(o["weatherProvider"], 0, cfg::units::WEATHER_PROVIDER_COUNT - 1);
  if (o["metOfficeApiKey"].is<const char*>())
    strlcpy(s.metOfficeApiKey, o["metOfficeApiKey"], sizeof(s.metOfficeApiKey));
  if (o["pirateWeatherApiKey"].is<const char*>())
    strlcpy(s.pirateWeatherApiKey, o["pirateWeatherApiKey"], sizeof(s.pirateWeatherApiKey));
  if (o["theme"].is<unsigned>()) s.theme = (uint8_t)clampU32(o["theme"], 0, cfg::theme::COUNT - 1);
  if (o["autoPageRotation"].is<bool>()) s.autoPageRotation = o["autoPageRotation"] ? 1 : 0;
  if (o["pageDurationS"].is<unsigned>())
    s.pageDurationS = (uint16_t)clampU32(o["pageDurationS"], cfg::ui::PAGE_DURATION_MIN_S,
                                          cfg::ui::PAGE_DURATION_MAX_S);
  if (o["hourlyShiftS"].is<unsigned>())
    s.hourlyShiftS = (uint16_t)clampU32(o["hourlyShiftS"], cfg::ui::HOURLY_SHIFT_MIN_S,
                                         cfg::ui::HOURLY_SHIFT_MAX_S);
  if (o["weatherRefreshS"].is<unsigned long>())
    s.weatherRefreshS = clampU32(o["weatherRefreshS"], cfg::weather::REFRESH_MIN_S,
                                  cfg::weather::REFRESH_MAX_S);
  if (o["colorOrder"].is<unsigned>())
    s.colorOrder = (uint8_t)clampU32(o["colorOrder"], 0, cfg::display::COLOR_ORDER_COUNT - 1);
  if (o["displayFlipped"].is<bool>()) s.displayFlipped = o["displayFlipped"] ? 1 : 0;
  if (o["mirrorH"].is<bool>()) s.mirrorH = o["mirrorH"] ? 1 : 0;
  if (o["mirrorV"].is<bool>()) s.mirrorV = o["mirrorV"] ? 1 : 0;
  if (o["invertColors"].is<bool>()) s.invertColors = o["invertColors"] ? 1 : 0;
  if (o["rotate90"].is<bool>()) s.rotate90 = o["rotate90"] ? 1 : 0;
  if (o["swapNavigation"].is<bool>()) s.swapNavigation = o["swapNavigation"] ? 1 : 0;
  if (o["firstBootCompleted"].is<bool>()) s.firstBootCompleted = o["firstBootCompleted"] ? 1 : 0;
  if (o["alertsEnabled"].is<bool>()) s.alertsEnabled = o["alertsEnabled"] ? 1 : 0;
  if (!o["alertWindWarningKmh"].isNull())
    s.alertWindWarningKmh =
        clampF(o["alertWindWarningKmh"], cfg::alerts::WIND_MIN_KMH, cfg::alerts::WIND_MAX_KMH);
  if (!o["alertWindDangerKmh"].isNull())
    s.alertWindDangerKmh =
        clampF(o["alertWindDangerKmh"], cfg::alerts::WIND_MIN_KMH, cfg::alerts::WIND_MAX_KMH);
  if (!o["alertRainProbWarningPct"].isNull())
    s.alertRainProbWarningPct = clampF(o["alertRainProbWarningPct"],
                                        cfg::alerts::RAIN_PROB_MIN_PCT, cfg::alerts::RAIN_PROB_MAX_PCT);
  if (!o["alertHeatWarningC"].isNull())
    s.alertHeatWarningC =
        clampF(o["alertHeatWarningC"], cfg::alerts::TEMP_MIN_C, cfg::alerts::TEMP_MAX_C);
  if (!o["alertColdWarningC"].isNull())
    s.alertColdWarningC =
        clampF(o["alertColdWarningC"], cfg::alerts::TEMP_MIN_C, cfg::alerts::TEMP_MAX_C);
  if (!o["alertUvWarningIndex"].isNull())
    s.alertUvWarningIndex =
        clampF(o["alertUvWarningIndex"], cfg::alerts::UV_MIN_INDEX, cfg::alerts::UV_MAX_INDEX);
  if (!o["alertPressureChangeHpa"].isNull())
    s.alertPressureChangeHpa = clampF(o["alertPressureChangeHpa"],
                                       cfg::alerts::PRESSURE_CHANGE_MIN_HPA,
                                       cfg::alerts::PRESSURE_CHANGE_MAX_HPA);
  return true;
}

}  // namespace SettingsBackup
