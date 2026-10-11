#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>

#include <functional>

#include "display/DisplayManager.h"
#include "network/NetworkManager.h"
#include "storage/SdCardManager.h"
#include "storage/SettingsManager.h"
#include "weather/WeatherService.h"

// HTTP settings portal + JSON API (reachable once on WiFi — see
// network/WifiProvisioning.h for how the device gets there before this even
// starts):
//   GET  /                     portal SPA (PROGMEM)
//   GET  /api/status           device + network + weather-fetch status
//   GET  /api/settings         current settings (wifi password masked)
//   POST /api/settings         partial update, JSON body -> deferred apply
//   POST /api/restart          deferred restart (response flushes first)
//   POST /api/recal            3.5" board only: forget the touch calibration and
//                              restart (the next boot asks for the four corners)
//   GET  /api/settings/backup  full settings as a downloadable JSON file
//                              (unmasked secrets — see storage/SettingsBackup)
//   POST /api/settings/restore JSON body from a previous backup -> apply,
//                              save, deferred restart
//   GET  /api/sdcard/status    {"present": bool}
//   POST /api/sdcard/backup    write the same backup JSON to the SD card
//   POST /api/sdcard/restore   read it back from the SD card and apply
//   POST /update              firmware image (multipart upload, field "firmware"):
//                              written to the idle app slot, then restart
//   GET  /mirror               screen-mirror viewer page (manual/auto refresh)
//   GET  /mirror.bmp           current TFT contents as a 24-bit BMP, read
//                              back over SPI row-by-row (no framebuffer
//                              exists to read from directly) — stalls the
//                              device's UI/touch for the read duration
//
// Factory reset is physical-only: holding the BOOT button for
// cfg::sys::FACTORY_RESET_HOLD_MS (see main.cpp) wipes settings (and the
// saved WiFi credentials — see WifiProvisioning::reset()) and reboots
// directly, no web API involved.
//
// To add future settings to the portal:
//   1. Append the field to AppSettings (storage/SettingsManager.h) and bump
//      cfg::storage::SETTINGS_SCHEMA (freeze the old layout for migration).
//   2. Add a default/bounds constant under cfg:: (config/AppConfig.h).
//   3. Add a <input>/<select> in WebAssets.h whose `name` equals the JSON key
//      equals the struct member — the portal JS reads/writes generically by
//      element type (checkbox/number/range/select vs. text), no JS changes
//      needed.
//   4. Add one clamp line in handlePostSettings() and one line in
//      handleGetSettings().
class WebPortal {
 public:
  using SettingsChangedCallback = std::function<void()>;
  using RecalibrateCallback = std::function<void()>;

  WebPortal(SettingsManager& settings, NetworkManager& network, WeatherService& weather,
            SdCardManager& sdCard, DisplayManager& display);

  bool begin();
  void handle();  // web.handle task: serve clients + execute deferred restarts
  void onSettingsChanged(SettingsChangedCallback cb) { _onSettingsChanged = cb; }
  // Boards that need a touch calibration (3.5"): enables POST /api/recal, which
  // runs this (forget the calibration) and restarts so the next boot asks again.
  void onRecalibrate(RecalibrateCallback cb) { _onRecalibrate = cb; }

 private:
  void handleRoot();
  void handleStatus();
  void handleGetSettings();
  void handlePostSettings();
  void handleRestart();
  void handleRecalibrate();
  void handleSettingsBackup();
  void handleSettingsRestore();
  void handleSdCardStatus();
  void handleSdCardBackup();
  void handleSdCardRestore();
  void handleMirror();
  void handleUpdateDone();
  void handleUpdateUpload();
  void handleMirrorImage();
  void handleNotFound();
  void sendJson(int code, const JsonDocument& doc);
  void sendOk(bool restartRequired = false);
  void scheduleRestart();

  SettingsManager& _settings;
  NetworkManager& _network;
  WeatherService& _weather;
  SdCardManager& _sdCard;
  DisplayManager& _display;
  WebServer _http;
  SettingsChangedCallback _onSettingsChanged;
  RecalibrateCallback _onRecalibrate;
  uint32_t _restartAtMs = 0;
  uint32_t _applyAtMs = 0;
  bool _updateOk = false;
  bool _updateFirstChunk = false;
  String _updateError;
};
