#pragma once

#include <Arduino.h>

#include "config/AppConfig.h"

// One logged reading. Deliberately its own vocabulary, not WeatherData —
// this module doesn't know Open-Meteo or WeatherService exist (see
// CODE STRUCTURE: storage stays separate from API/display code). Whatever
// calls addReading() (main.cpp) is responsible for mapping a WeatherData
// snapshot into one of these.
struct WeatherHistoryReading {
  char timestamp[20] = {0};  // "YYYY-MM-DDTHH:MM", copied straight from
                             // WeatherData::isoTime — kept as the log's time
                             // source (not network/TimeManager.h's NTP clock)
                             // so every entry stays anchored to the same
                             // fetch its other columns came from
  float temperatureC = 0.0f;
  int8_t humidityPct = 0;
  float pressureHpa = 0.0f;
  float windSpeedKmh = 0.0f;
  int8_t rainProbabilityPct = 0;
  int8_t cloudCoverPct = 0;
  int16_t weatherCode = -1;
};

// Owns a flat CSV log of weather readings on LittleFS, for future trend/
// graph pages (24h graph, 7-day graph, 30-day summary) — this step only
// builds the log itself, not any UI that reads it back for graphing.
//
// One raw-resolution file, not per-resolution aggregate tables: the 24h/7d/
// 30d views the brief mentions are all just different-sized *slices* of the
// same log (a future graph page reads the newest N rows and downsamples if
// it wants to) — building separate rollup tables now would be the kind of
// database-shaped over-engineering the task explicitly warns against for
// ~230 KB of raw data.
//
// Every LittleFS call in this class happens on the same thread that calls
// it (no background task) — deliberately, so there is exactly one thread
// ever touching the filesystem, and no mutex is needed. addReading() is a
// single small file append (fast — a few ms at most) so this is safe to
// call directly from a scheduler task. The one exception is the (rare, at
// most once every ~3 days once the log is mature) trim that runs when
// MAX_READINGS is exceeded, which streams the file line-by-line rather than
// loading it into RAM — see WeatherHistoryManager.cpp.
class WeatherHistoryManager {
 public:
  static WeatherHistoryManager& instance();

  WeatherHistoryManager(const WeatherHistoryManager&) = delete;
  WeatherHistoryManager& operator=(const WeatherHistoryManager&) = delete;

  // Mounts LittleFS (formatting it if unformatted/corrupt — safe on a fresh
  // board or after a partial-format from a prior crash) and creates the
  // history file if it doesn't exist yet. Recovers the reading count and
  // last reading from a small sidecar file (cfg::history::META_PATH) rather
  // than scanning the whole log — O(1), not O(readings), so a mature
  // multi-thousand-row log never slows down boot. Only falls back to an
  // actual full-log scan if that sidecar is missing or unreadable (first
  // boot ever, or the rare case of a power loss mid-write to it) — see
  // rebuildMetaFromLog() in the .cpp. Returns false only if LittleFS itself
  // could not be mounted.
  bool begin();

  // Appends one reading, formatted as CSV. Rate-limited to at most once
  // every cfg::history::MIN_INTERVAL_S — callers do not need their own
  // throttling and can safely call this after every successful weather
  // update; the very first call (since boot, or since clearHistory())
  // always goes through immediately, satisfying "record at startup".
  // Silently trims the oldest readings if this push exceeds
  // cfg::history::MAX_READINGS. No-op (and returns immediately) if
  // LittleFS failed to mount.
  void addReading(const WeatherHistoryReading& reading);

  // Copies the most recently logged reading into `out`. Returns false (and
  // leaves `out` unchanged) if nothing has been logged yet.
  bool getLatestReading(WeatherHistoryReading& out) const;

  // Reads up to `maxCount` of the most recent readings, oldest-first, into
  // `out` (caller-owned array). Returns how many were actually filled — may
  // be less than `maxCount` if the log has fewer readings than that, and
  // silently skips any single malformed/truncated line it encounters (see
  // .cpp — the one line most likely to be mid-write during a power loss).
  size_t getHistory(WeatherHistoryReading* out, size_t maxCount) const;

  // Erases the entire log and starts fresh. The next addReading() call
  // afterwards is treated like a first-ever reading (goes through
  // immediately, ignoring the rate limit).
  void clearHistory();

  struct StorageUsage {
    size_t totalBytes = 0;
    size_t usedBytes = 0;
    size_t readingCount = 0;
    char lastLoggedTime[20] = {0};  // "" if nothing has been logged yet
  };
  // Cheap (no file scan) — safe to call every UI refresh tick, e.g. from
  // pages/SystemPage.cpp.
  StorageUsage getStorageUsage() const;

 private:
  WeatherHistoryManager() = default;

  bool appendLine(const WeatherHistoryReading& reading);
  void trimOldest();
  bool loadMeta();               // false if the sidecar is missing/unreadable
  void saveMeta();                // rewrites the (few-byte) sidecar
  void rebuildMetaFromLog();      // full-log fallback recovery — see begin()
  static bool parseLine(const String& line, WeatherHistoryReading& out);
  static void formatLine(const WeatherHistoryReading& reading, char* buf, size_t bufLen);

  bool _mounted = false;
  bool _everWritten = false;
  uint32_t _lastWriteMs = 0;
  size_t _readingCount = 0;
  bool _hasLastReading = false;
  WeatherHistoryReading _lastReading;
};
