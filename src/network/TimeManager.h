#pragma once

#include <Arduino.h>

#include "config/AppConfig.h"
#include "storage/SettingsManager.h"

// Wraps the ESP32's SNTP client (configTzTime) to give the device a real
// wall clock. Before this existed, every on-screen "now" (StatusBar's top
// bar, moon rise/set, Met.no's local-time conversion, ...) was a best-effort
// estimate anchored off the last weather fetch — see weather/WeatherData.h.
// StatusBar now prefers this once synced, falling back to that estimate
// until the first sync lands (see ui/StatusBar.cpp).
//
// Non-blocking and polled like every other manager here: requestSync() kicks
// off SNTP and returns immediately; tick() polls time(nullptr) against a
// "does this look like a real date" threshold rather than blocking on
// getLocalTime()'s timeout. Reuses AppSettings::timezone (the same POSIX TZ
// string Open-Meteo's timezone=auto already assumes) so the synced clock is
// local time, DST included, not UTC.
class TimeManager {
 public:
  // Stores the configured server/timezone; does not sync yet — there's no
  // WiFi at boot. Call requestSync() once connected (main.cpp does this from
  // NetworkManager::onStateChange).
  void begin(const AppSettings& settings);

  // Re-syncs immediately if the server or timezone actually changed.
  void applySettings(const AppSettings& settings);

  // Kicks off (or re-kicks-off) an SNTP sync. Safe to call any time,
  // including while one is already pending.
  void requestSync();

  // Call frequently from the main scheduler. Polls for the pending sync's
  // completion/timeout, and fires the 24h periodic re-sync once idle.
  void tick(uint32_t nowMs);

  bool isSynced() const { return _synced; }

 private:
  char _server[48] = {0};
  char _tz[48] = {0};
  bool _pendingSync = false;
  bool _synced = false;
  // Timestamp of the most recent sync *attempt* (successful or not) — reused
  // both to time out a pending sync and to pace the 24h idle re-sync, since
  // the timeout (seconds) is always far shorter than the resync interval
  // (24h) and the two never need to be told apart.
  uint32_t _lastAttemptMs = 0;
};
