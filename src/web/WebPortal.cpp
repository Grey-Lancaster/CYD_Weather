#include "web/WebPortal.h"

#include <new>

#include "config/AppConfig.h"
#include "config/Version.h"
#include "storage/SettingsBackup.h"
#include "utils/Format.h"
#include "utils/Log.h"
#include "utils/ResetReason.h"
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
                     SdCardManager& sdCard, DisplayManager& display)
    : _settings(settings), _network(network), _weather(weather), _sdCard(sdCard),
      _display(display), _http(cfg::web::PORT) {}

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
  _http.on("/mirror", HTTP_GET, [this]() { handleMirror(); });
  _http.on("/mirror.bmp", HTTP_GET, [this]() { handleMirrorImage(); });
  _http.onNotFound([this]() { handleNotFound(); });
  _http.begin();
  LOGI(TAG, "portal listening on port %u", (unsigned)cfg::web::PORT);
  return true;
}

void WebPortal::handle() {
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

void WebPortal::handleRoot() {
  _http.send_P(200, "text/html", WEB_INDEX_HTML);
}

void WebPortal::handleMirror() {
  _http.send_P(200, "text/html", WEB_MIRROR_HTML);
}

// Streams the current TFT contents as a 24-bit uncompressed BMP. There's no
// software framebuffer for the whole screen to read from — a full-screen
// buffer doesn't fit ESP32 heap (see cfg::display::CANVAS_MAX_BYTES's
// comment in AppConfig.h) — so this reads the panel back over SPI in row
// chunks via TFT_eSPI's readRectRGB(), converting each chunk from RGB to
// BMP's BGR byte order and reversing its row order (BMP wants bottom row
// first) before sending it in one call. Chunked rather than one row at a
// time — confirmed live that doing this row-by-row (240 separate
// SPI-read/HTTP-send round trips) took ~20s; batching into kChunkRows-sized
// chunks (heap-allocated, since kChunkRows*960 bytes is too big to put on
// the stack safely) cuts that down to a handful of round trips. This still
// runs synchronously inside the same cooperative main loop as
// rendering/touch/weather (see TaskScheduler), so the device's UI briefly
// stalls for the capture — acceptable for an occasional manual snapshot,
// not meant to be polled quickly or continuously.
void WebPortal::handleMirrorImage() {
  TFT_eSPI& tft = _display.tft();
  const int32_t w = (int32_t)cfg::display::WIDTH;
  const int32_t h = (int32_t)cfg::display::HEIGHT;
  const int32_t rowBytes = w * 3;
  const int32_t rowPad = (4 - (rowBytes % 4)) % 4;
  const uint32_t pixelDataSize = (uint32_t)(rowBytes + rowPad) * (uint32_t)h;
  const uint32_t fileSize = 54u + pixelDataSize;

  uint8_t header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  auto putU32 = [&](size_t offset, uint32_t v) {
    header[offset] = (uint8_t)(v & 0xFF);
    header[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
    header[offset + 2] = (uint8_t)((v >> 16) & 0xFF);
    header[offset + 3] = (uint8_t)((v >> 24) & 0xFF);
  };
  putU32(2, fileSize);
  putU32(10, 54);              // bfOffBits
  putU32(14, 40);              // biSize (BITMAPINFOHEADER)
  putU32(18, (uint32_t)w);
  putU32(22, (uint32_t)h);     // positive height = bottom-up row order
  header[26] = 1;              // biPlanes
  header[28] = 24;             // biBitCount
  putU32(34, pixelDataSize);

  _http.setContentLength(fileSize);
  _http.send(200, "image/bmp", "");
  _http.sendContent((const char*)header, sizeof(header));

  // TEMP DIAGNOSTIC: ?mode=0..3 selects a candidate colour transform so
  // several hypotheses can be compared in one flash instead of one per
  // rebuild. 0=raw passthrough, 1=swap R/B, 2=invert bytes, 3=swap+invert.
  // Also toggles display inversion off for the read itself, in case this
  // panel's RAMRD is unreliable while INVON is active.
  const int debugMode = _http.hasArg("mode") ? _http.arg("mode").toInt() : 1;
  const bool needsSwap = (debugMode == 1 || debugMode == 3);
  const bool needsInvert = (debugMode == 2 || debugMode == 3);
  const bool disableInversionForRead = _http.hasArg("noinv");
  if (disableInversionForRead) tft.invertDisplay(false);

  constexpr int32_t kChunkRows = 40;  // 40 * 960B = 37.5KB, well clear of ~200KB free heap
  const bool forceSingleRow = _http.hasArg("single");
  uint8_t* chunkBuf =
      forceSingleRow ? nullptr : new (std::nothrow) uint8_t[(size_t)kChunkRows * (size_t)rowBytes];
  static const uint8_t kPad[3] = {0, 0, 0};

  if (chunkBuf == nullptr) {
    // Allocation failed (fragmented/low heap) — fall back to the slow but
    // minimal-memory single-row path rather than failing the capture.
    uint8_t row[cfg::display::WIDTH * 3];
    for (int32_t y = h - 1; y >= 0; y--) {
      tft.readRectRGB(0, y, w, 1, row);
      if (needsSwap) {
        for (int32_t x = 0; x < w; x++) {
          const uint8_t r = row[x * 3];
          row[x * 3] = row[x * 3 + 2];
          row[x * 3 + 2] = r;
        }
      }
      if (needsInvert) {
        for (int32_t i = 0; i < rowBytes; i++) row[i] = (uint8_t)(255 - row[i]);
      }
      _http.sendContent((const char*)row, (size_t)rowBytes);
      if (rowPad > 0) _http.sendContent((const char*)kPad, (size_t)rowPad);
    }
    if (disableInversionForRead) tft.invertDisplay(_settings.get().invertColors != 0);
    return;
  }

  int32_t yEnd = h;  // exclusive top of the not-yet-sent region
  while (yEnd > 0) {
    const int32_t rows = min(kChunkRows, yEnd);
    const int32_t yStart = yEnd - rows;
    tft.readRectRGB(0, yStart, w, rows, chunkBuf);

    const int32_t pixelsInChunk = rows * w;
    if (needsSwap) {
      for (int32_t p = 0; p < pixelsInChunk; p++) {
        uint8_t* px = chunkBuf + (size_t)p * 3;
        const uint8_t r = px[0];
        px[0] = px[2];
        px[2] = r;
      }
    }
    if (needsInvert) {
      const int32_t bytesInChunk = pixelsInChunk * 3;
      for (int32_t i = 0; i < bytesInChunk; i++) chunkBuf[i] = (uint8_t)(255 - chunkBuf[i]);
    }

    if (rowPad == 0) {
      // Common case (true for this panel's fixed 320px width, where
      // rowBytes is already a multiple of 4): reverse the chunk's row
      // order in place and ship it as one contiguous write.
      for (int32_t a = 0, b = rows - 1; a < b; a++, b--) {
        uint8_t* rowA = chunkBuf + (size_t)a * rowBytes;
        uint8_t* rowB = chunkBuf + (size_t)b * rowBytes;
        for (int32_t i = 0; i < rowBytes; i++) {
          const uint8_t t = rowA[i];
          rowA[i] = rowB[i];
          rowB[i] = t;
        }
      }
      _http.sendContent((const char*)chunkBuf, (size_t)rows * (size_t)rowBytes);
    } else {
      for (int32_t r = rows - 1; r >= 0; r--) {
        _http.sendContent((const char*)(chunkBuf + (size_t)r * rowBytes), (size_t)rowBytes);
        _http.sendContent((const char*)kPad, (size_t)rowPad);
      }
    }

    yEnd -= rows;
  }

  if (disableInversionForRead) tft.invertDisplay(_settings.get().invertColors != 0);
  delete[] chunkBuf;
}

void WebPortal::handleStatus() {
  JsonDocument doc;
  doc["name"] = FW_NAME;
  doc["version"] = FW_VERSION;
  doc["build"] = FW_BUILD_DATE;
  char up[24];
  fmt::uptime(up, sizeof(up), millis());
  doc["uptime"] = up;
  doc["lastRestart"] = resetReasonText();
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
  doc["hour12"] = s.hour12 != 0;
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
    // Posting the mask back means "leave the stored key unchanged".
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
  if (doc["hour12"].is<bool>()) {
    s.hour12 = doc["hour12"].as<bool>() ? 1 : 0;
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
