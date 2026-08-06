#include "web/WebPortal.h"

#include <WiFi.h>

#include "config/AppConfig.h"
#include "config/Version.h"
#include "storage/SettingsBackup.h"
#include "utils/Format.h"
#include "utils/Log.h"
#include "weather/WeatherUnits.h"
#include "web/WebAssets.h"

static const char* TAG = "WEB";

static uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float clampF(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

WebPortal::WebPortal(SettingsManager& settings, NetworkManager& network, WeatherService& weather,
                     SdCardManager& sdCard)
    : _settings(settings), _network(network), _weather(weather), _sdCard(sdCard),
      _http(cfg::web::PORT) {}

bool WebPortal::begin() {
  _http.on("/", HTTP_GET, [this]() { handleRoot(); });
  _http.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  _http.on("/api/settings", HTTP_GET, [this]() { handleGetSettings(); });
  _http.on("/api/settings", HTTP_POST, [this]() { handlePostSettings(); });
  _http.on("/api/restart", HTTP_POST, [this]() { handleRestart(); });
  _http.on("/api/settings/backup", HTTP_GET, [this]() { handleSettingsBackup(); });
  _http.on("/api/settings/restore", HTTP_POST, [this]() { handleSettingsRestore(); });
  _http.on("/api/sdcard/status", HTTP_GET, [this]() { handleSdCardStatus(); });
  _http.on("/api/sdcard/backup", HTTP_POST, [this]() { handleSdCardBackup(); });
  _http.on("/api/sdcard/restore", HTTP_POST, [this]() { handleSdCardRestore(); });
  _http.onNotFound([this]() { handleNotFound(); });
  _http.begin();
  LOGI(TAG, "portal listening on port %u", (unsigned)cfg::web::PORT);
  return true;
}

void WebPortal::handle() {
  updateCaptiveDns();
  if (_captiveActive) _dns.processNextRequest();
  _http.handleClient();
  if (_applyAtMs != 0 && (int32_t)(millis() - _applyAtMs) >= 0) {
    _applyAtMs = 0;
    if (_onSettingsChanged) _onSettingsChanged();
  }
  if (_restartAtMs != 0 && (int32_t)(millis() - _restartAtMs) >= 0) {
    LOGW(TAG, "restarting now");
    Serial.flush();
    ESP.restart();
  }
}

void WebPortal::updateCaptiveDns() {
  const bool wantCaptive = _network.state() == NetState::ApMode;
  if (wantCaptive == _captiveActive) return;
  if (wantCaptive) {
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _captiveActive = _dns.start(cfg::web::DNS_PORT, "*", WiFi.softAPIP());
    if (_captiveActive) {
      LOGI(TAG, "captive portal DNS up (all hosts -> %s)",
           WiFi.softAPIP().toString().c_str());
    } else {
      LOGE(TAG, "captive portal DNS failed to start");
    }
  } else {
    _dns.stop();
    _captiveActive = false;
    LOGI(TAG, "captive portal DNS stopped");
  }
}

void WebPortal::redirectToPortal() {
  _http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  _http.send(302, "text/plain", "");
}

void WebPortal::handleRoot() {
  _http.send_P(200, "text/html", WEB_INDEX_HTML);
}

void WebPortal::handleStatus() {
  JsonDocument doc;
  doc["name"] = FW_NAME;
  doc["version"] = FW_VERSION;
  doc["build"] = FW_BUILD_DATE;
  char up[24];
  fmt::uptime(up, sizeof(up), millis());
  doc["uptime"] = up;
  doc["heapFree"] = ESP.getFreeHeap();
  doc["heapMin"] = ESP.getMinFreeHeap();
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["state"] = _network.stateName();
  wifi["ssid"] = _network.ssid();
  wifi["ip"] = _network.ipAddress();
  wifi["rssi"] = _network.rssi();
  wifi["hostname"] = _network.hostname();

  // Lets the web UI confirm a settings change actually landed (e.g.
  // switching weather providers) instead of just trusting a static "Saved"
  // message — see WebAssets.h, which polls this and watches lastUpdate move.
  JsonObject weather = doc["weather"].to<JsonObject>();
  const WeatherData wd = _weather.data();
  weather["provider"] = weatherProviderName((WeatherProvider)_settings.get().weatherProvider);
  weather["valid"] = wd.valid;
  weather["lastUpdate"] = wd.valid ? wd.lastUpdate : "";
  weather["failed"] = _weather.lastFetchFailed();

  sendJson(200, doc);
}

void WebPortal::handleGetSettings() {
  const AppSettings& s = _settings.get();
  JsonDocument doc;
  doc["wifiSsid"] = s.wifiSsid;
  doc["wifiPass"] = s.wifiPass[0] != '\0' ? cfg::web::PASS_MASK : "";
  doc["hostname"] = s.hostname;
  doc["ntpServer"] = s.ntpServer;
  doc["brightness"] = s.brightness;
  doc["dimBrightness"] = s.dimBrightness;
  doc["dimTimeoutS"] = s.dimTimeoutS;
  doc["logLevel"] = s.logLevel;
  doc["touchXMin"] = s.touchXMin;
  doc["touchXMax"] = s.touchXMax;
  doc["touchYMin"] = s.touchYMin;
  doc["touchYMax"] = s.touchYMax;
  doc["locationName"] = s.locationName;
  doc["latitude"] = s.latitude;
  doc["longitude"] = s.longitude;
  doc["timezone"] = s.timezone;
  doc["tempUnit"] = s.tempUnit;
  doc["windUnit"] = s.windUnit;
  doc["pressureUnit"] = s.pressureUnit;
  doc["weatherProvider"] = s.weatherProvider;
  doc["metOfficeApiKey"] = s.metOfficeApiKey[0] != '\0' ? cfg::web::PASS_MASK : "";
  doc["pirateWeatherApiKey"] = s.pirateWeatherApiKey[0] != '\0' ? cfg::web::PASS_MASK : "";
  doc["theme"] = s.theme;
  doc["autoPageRotation"] = s.autoPageRotation != 0;
  doc["pageDurationS"] = s.pageDurationS;
  doc["hourlyShiftS"] = s.hourlyShiftS;
  doc["weatherRefreshS"] = s.weatherRefreshS;
  doc["colorOrder"] = s.colorOrder;
  doc["displayFlipped"] = s.displayFlipped;
  doc["mirrorH"] = s.mirrorH != 0;
  doc["invertColors"] = s.invertColors != 0;
  doc["rotate90"] = s.rotate90 != 0;
  doc["swapNavigation"] = s.swapNavigation != 0;
  sendJson(200, doc);
}

void WebPortal::handlePostSettings() {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, _http.arg("plain"));
  if (err) {
    JsonDocument res;
    res["ok"] = false;
    res["error"] = "invalid json";
    sendJson(400, res);
    return;
  }

  AppSettings& s = _settings.get();
  bool restartRequired = false;

  if (doc["wifiSsid"].is<const char*>()) {
    strlcpy(s.wifiSsid, doc["wifiSsid"].as<const char*>(), sizeof(s.wifiSsid));
  }
  if (doc["wifiPass"].is<const char*>()) {
    const char* pass = doc["wifiPass"].as<const char*>();
    // Posting the mask back means "leave the stored password unchanged".
    if (strcmp(pass, cfg::web::PASS_MASK) != 0) {
      strlcpy(s.wifiPass, pass, sizeof(s.wifiPass));
    }
  }
  if (doc["hostname"].is<const char*>()) {
    const char* hostname = doc["hostname"].as<const char*>();
    if (hostname[0] != '\0' && strcmp(hostname, s.hostname) != 0) {
      strlcpy(s.hostname, hostname, sizeof(s.hostname));
      restartRequired = true;  // hostname/mDNS are applied at boot
    }
  }
  if (doc["ntpServer"].is<const char*>()) {
    const char* ntpServer = doc["ntpServer"].as<const char*>();
    if (ntpServer[0] != '\0') strlcpy(s.ntpServer, ntpServer, sizeof(s.ntpServer));
  }
  if (doc["brightness"].is<unsigned>()) {
    s.brightness =
        (uint8_t)clampU32(doc["brightness"].as<unsigned>(), cfg::display::MIN_BRIGHTNESS, 255);
  }
  if (doc["dimBrightness"].is<unsigned>()) {
    s.dimBrightness = (uint8_t)clampU32(doc["dimBrightness"].as<unsigned>(), 0, 255);
  }
  if (doc["dimTimeoutS"].is<unsigned>()) {
    s.dimTimeoutS = (uint16_t)clampU32(doc["dimTimeoutS"].as<unsigned>(), 0, 65535);
  }
  if (doc["logLevel"].is<unsigned>()) {
    s.logLevel = (uint8_t)clampU32(doc["logLevel"].as<unsigned>(), 0, 4);
  }
  if (doc["touchXMin"].is<unsigned>()) s.touchXMin = (uint16_t)clampU32(doc["touchXMin"].as<unsigned>(), 0, 4095);
  if (doc["touchXMax"].is<unsigned>()) s.touchXMax = (uint16_t)clampU32(doc["touchXMax"].as<unsigned>(), 0, 4095);
  if (doc["touchYMin"].is<unsigned>()) s.touchYMin = (uint16_t)clampU32(doc["touchYMin"].as<unsigned>(), 0, 4095);
  if (doc["touchYMax"].is<unsigned>()) s.touchYMax = (uint16_t)clampU32(doc["touchYMax"].as<unsigned>(), 0, 4095);

  if (doc["locationName"].is<const char*>()) {
    strlcpy(s.locationName, doc["locationName"].as<const char*>(), sizeof(s.locationName));
  }
  if (!doc["latitude"].isNull()) {
    s.latitude = clampF(doc["latitude"].as<float>(), cfg::location::LAT_MIN, cfg::location::LAT_MAX);
  }
  if (!doc["longitude"].isNull()) {
    s.longitude = clampF(doc["longitude"].as<float>(), cfg::location::LON_MIN, cfg::location::LON_MAX);
  }
  if (doc["timezone"].is<const char*>()) {
    strlcpy(s.timezone, doc["timezone"].as<const char*>(), sizeof(s.timezone));
  }
  if (doc["tempUnit"].is<unsigned>()) {
    s.tempUnit = (uint8_t)clampU32(doc["tempUnit"].as<unsigned>(), 0, cfg::units::TEMP_COUNT - 1);
  }
  if (doc["windUnit"].is<unsigned>()) {
    s.windUnit = (uint8_t)clampU32(doc["windUnit"].as<unsigned>(), 0, cfg::units::WIND_COUNT - 1);
  }
  if (doc["pressureUnit"].is<unsigned>()) {
    s.pressureUnit =
        (uint8_t)clampU32(doc["pressureUnit"].as<unsigned>(), 0, cfg::units::PRESSURE_COUNT - 1);
  }
  if (doc["weatherProvider"].is<unsigned>()) {
    s.weatherProvider = (uint8_t)clampU32(doc["weatherProvider"].as<unsigned>(), 0,
                                          cfg::units::WEATHER_PROVIDER_COUNT - 1);
  }
  if (doc["metOfficeApiKey"].is<const char*>()) {
    const char* key = doc["metOfficeApiKey"].as<const char*>();
    // Posting the mask back means "leave the stored key unchanged" — same
    // convention as wifiPass above.
    if (strcmp(key, cfg::web::PASS_MASK) != 0) {
      strlcpy(s.metOfficeApiKey, key, sizeof(s.metOfficeApiKey));
    }
  }
  if (doc["pirateWeatherApiKey"].is<const char*>()) {
    const char* key = doc["pirateWeatherApiKey"].as<const char*>();
    if (strcmp(key, cfg::web::PASS_MASK) != 0) {
      strlcpy(s.pirateWeatherApiKey, key, sizeof(s.pirateWeatherApiKey));
    }
  }
  if (doc["theme"].is<unsigned>()) {
    s.theme = (uint8_t)clampU32(doc["theme"].as<unsigned>(), 0, cfg::theme::COUNT - 1);
  }
  if (doc["autoPageRotation"].is<bool>()) {
    s.autoPageRotation = doc["autoPageRotation"].as<bool>() ? 1 : 0;
  }
  if (doc["pageDurationS"].is<unsigned>()) {
    s.pageDurationS = (uint16_t)clampU32(doc["pageDurationS"].as<unsigned>(),
                                         cfg::ui::PAGE_DURATION_MIN_S, cfg::ui::PAGE_DURATION_MAX_S);
  }
  if (doc["hourlyShiftS"].is<unsigned>()) {
    s.hourlyShiftS = (uint16_t)clampU32(doc["hourlyShiftS"].as<unsigned>(),
                                        cfg::ui::HOURLY_SHIFT_MIN_S, cfg::ui::HOURLY_SHIFT_MAX_S);
  }
  if (doc["weatherRefreshS"].is<unsigned long>()) {
    s.weatherRefreshS = clampU32(doc["weatherRefreshS"].as<unsigned long>(),
                                 cfg::weather::REFRESH_MIN_S, cfg::weather::REFRESH_MAX_S);
  }
  if (doc["colorOrder"].is<unsigned>()) {
    s.colorOrder =
        (uint8_t)clampU32(doc["colorOrder"].as<unsigned>(), 0, cfg::display::COLOR_ORDER_COUNT - 1);
  }
  if (doc["displayFlipped"].is<unsigned>()) {
    s.displayFlipped = doc["displayFlipped"].as<unsigned>() != 0 ? 1 : 0;
  }
  if (doc["mirrorH"].is<bool>()) {
    s.mirrorH = doc["mirrorH"].as<bool>() ? 1 : 0;
  }
  if (doc["invertColors"].is<bool>()) {
    s.invertColors = doc["invertColors"].as<bool>() ? 1 : 0;
  }
  if (doc["rotate90"].is<bool>()) {
    s.rotate90 = doc["rotate90"].as<bool>() ? 1 : 0;
  }
  if (doc["swapNavigation"].is<bool>()) {
    s.swapNavigation = doc["swapNavigation"].as<bool>() ? 1 : 0;
  }

  _settings.save();
  // Apply after the response has flushed: applying new WiFi credentials tears
  // down the AP/STA link, which would otherwise kill this very request.
  _applyAtMs = millis() + cfg::web::APPLY_DELAY_MS;
  LOGI(TAG, "settings updated via API, applying in %lu ms",
       (unsigned long)cfg::web::APPLY_DELAY_MS);
  sendOk(restartRequired);
}

void WebPortal::handleRestart() {
  sendOk();
  scheduleRestart();
}

void WebPortal::handleSettingsBackup() {
  JsonDocument doc;
  SettingsBackup::toJson(_settings.get(), doc);
  String body;
  serializeJson(doc, body);
  _http.sendHeader("Content-Disposition", "attachment; filename=\"cyd-weather-settings.json\"");
  _http.send(200, "application/json", body);
}

void WebPortal::handleSettingsRestore() {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, _http.arg("plain"));
  if (err || !SettingsBackup::fromJson(doc, _settings.get())) {
    JsonDocument res;
    res["ok"] = false;
    res["error"] = "invalid backup file";
    sendJson(400, res);
    return;
  }
  LOGW(TAG, "settings restored from uploaded backup, restarting");
  _settings.save();
  sendOk(true);
  scheduleRestart();
}

void WebPortal::handleSdCardStatus() {
  JsonDocument doc;
  doc["present"] = _sdCard.isPresent();
  sendJson(200, doc);
}

void WebPortal::handleSdCardBackup() {
  JsonDocument doc;
  SettingsBackup::toJson(_settings.get(), doc);
  String body;
  serializeJson(doc, body);
  JsonDocument res;
  if (!_sdCard.writeFile(cfg::sdcard::BACKUP_PATH, body)) {
    res["ok"] = false;
    res["error"] = "SD write failed (card missing or full?)";
    sendJson(500, res);
    return;
  }
  res["ok"] = true;
  sendJson(200, res);
}

void WebPortal::handleSdCardRestore() {
  String body;
  JsonDocument res;
  if (!_sdCard.readFile(cfg::sdcard::BACKUP_PATH, body)) {
    res["ok"] = false;
    res["error"] = "no backup file on SD card";
    sendJson(404, res);
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err || !SettingsBackup::fromJson(doc, _settings.get())) {
    res["ok"] = false;
    res["error"] = "backup file on SD card is invalid";
    sendJson(400, res);
    return;
  }
  LOGW(TAG, "settings restored from SD card, restarting");
  _settings.save();
  sendOk(true);
  scheduleRestart();
}

void WebPortal::handleNotFound() {
  // In AP mode the OS connectivity probes (Android /generate_204, Apple
  // /hotspot-detect.html, Windows /connecttest.txt, ...) all land here with a
  // foreign Host header; redirecting them opens the OS captive-portal sheet.
  if (_captiveActive && _http.hostHeader() != WiFi.softAPIP().toString()) {
    redirectToPortal();
    return;
  }
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = "not found";
  sendJson(404, doc);
}

void WebPortal::sendJson(int code, const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  _http.send(code, "application/json", body);
}

void WebPortal::sendOk(bool restartRequired) {
  JsonDocument doc;
  doc["ok"] = true;
  doc["restartRequired"] = restartRequired;
  sendJson(200, doc);
}

void WebPortal::scheduleRestart() {
  _restartAtMs = millis() + cfg::web::RESTART_DELAY_MS;
  LOGW(TAG, "restart scheduled in %lu ms", (unsigned long)cfg::web::RESTART_DELAY_MS);
}
