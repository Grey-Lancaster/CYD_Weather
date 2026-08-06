#include "storage/WeatherHistoryManager.h"

#include <LittleFS.h>

#include "utils/Log.h"

static const char* TAG = "HIST";

WeatherHistoryManager& WeatherHistoryManager::instance() {
  static WeatherHistoryManager manager;
  return manager;
}

bool WeatherHistoryManager::begin() {
  _mounted = LittleFS.begin(/*formatOnFail=*/true);
  if (!_mounted) {
    LOGE(TAG, "LittleFS mount failed");
    return false;
  }

  if (!LittleFS.exists(cfg::history::FILE_PATH)) {
    File f = LittleFS.open(cfg::history::FILE_PATH, "w");
    if (f) f.close();
  }

  if (!loadMeta()) {
    LOGI(TAG, "no usable index — recovering from the log directly");
    rebuildMetaFromLog();
  }
  LOGI(TAG, "ready: %u readings, %u/%u bytes used", (unsigned)_readingCount,
       (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return true;
}

void WeatherHistoryManager::addReading(const WeatherHistoryReading& reading) {
  if (!_mounted) return;
  const uint32_t now = millis();
  if (_everWritten && (now - _lastWriteMs) < cfg::history::MIN_INTERVAL_S * 1000u) return;

  if (!appendLine(reading)) {
    LOGW(TAG, "failed to append reading");
    return;
  }

  _readingCount++;
  _lastReading = reading;
  _hasLastReading = true;
  _lastWriteMs = now;
  _everWritten = true;
  LOGI(TAG, "logged reading #%u at %s", (unsigned)_readingCount, reading.timestamp);

  if (_readingCount > cfg::history::MAX_READINGS) trimOldest();
  saveMeta();
}

bool WeatherHistoryManager::getLatestReading(WeatherHistoryReading& out) const {
  if (!_hasLastReading) return false;
  out = _lastReading;
  return true;
}

size_t WeatherHistoryManager::getHistory(WeatherHistoryReading* out, size_t maxCount) const {
  if (!_mounted || out == nullptr || maxCount == 0) return 0;
  File f = LittleFS.open(cfg::history::FILE_PATH, "r");
  if (!f) return 0;

  // The log is oldest-first (append-only); skip everything before the last
  // maxCount rows so the result is the *most recent* readings, still
  // oldest-first within that slice.
  const size_t skipCount = _readingCount > maxCount ? _readingCount - maxCount : 0;
  size_t seen = 0;
  size_t filled = 0;
  while (f.available() && filled < maxCount) {
    const String line = f.readStringUntil('\n');
    if (line.length() == 0) continue;
    if (seen < skipCount) {
      seen++;
      continue;
    }
    seen++;
    if (parseLine(line, out[filled])) filled++;
  }
  f.close();
  return filled;
}

void WeatherHistoryManager::clearHistory() {
  if (!_mounted) return;
  LittleFS.remove(cfg::history::FILE_PATH);
  File f = LittleFS.open(cfg::history::FILE_PATH, "w");
  if (f) f.close();
  LittleFS.remove(cfg::history::META_PATH);
  _readingCount = 0;
  _hasLastReading = false;
  _everWritten = false;  // next addReading() behaves like the very first one
}

WeatherHistoryManager::StorageUsage WeatherHistoryManager::getStorageUsage() const {
  StorageUsage usage;
  usage.totalBytes = _mounted ? LittleFS.totalBytes() : 0;
  usage.usedBytes = _mounted ? LittleFS.usedBytes() : 0;
  usage.readingCount = _readingCount;
  if (_hasLastReading) {
    strlcpy(usage.lastLoggedTime, _lastReading.timestamp, sizeof(usage.lastLoggedTime));
  }
  return usage;
}

bool WeatherHistoryManager::appendLine(const WeatherHistoryReading& reading) {
  File f = LittleFS.open(cfg::history::FILE_PATH, "a");
  if (!f) return false;
  char line[192];
  formatLine(reading, line, sizeof(line));
  const size_t written = f.print(line);
  f.close();
  return written > 0;
}

// Rare (at most once every ~3 days once the log is mature — see
// cfg::history::TRIM_TARGET) and deliberately streamed rather than loaded
// into RAM: reads the source file line-by-line, drops the oldest
// (_readingCount - TRIM_TARGET) lines, and copies the rest verbatim into a
// temp file before swapping it in. A single ~230 KB rewrite at this cadence
// is not the kind of "blocking write" the brief is guarding against (that's
// about not stalling on every fetch, not about this rare maintenance pass).
void WeatherHistoryManager::trimOldest() {
  const size_t dropCount = _readingCount - cfg::history::TRIM_TARGET;
  File src = LittleFS.open(cfg::history::FILE_PATH, "r");
  if (!src) return;
  File dst = LittleFS.open(cfg::history::TEMP_FILE_PATH, "w");
  if (!dst) {
    src.close();
    return;
  }

  size_t skipped = 0;
  size_t kept = 0;
  while (src.available()) {
    const String line = src.readStringUntil('\n');
    if (line.length() == 0) continue;
    if (skipped < dropCount) {
      skipped++;
      continue;
    }
    dst.print(line);
    dst.print('\n');
    kept++;
  }
  src.close();
  dst.close();

  LittleFS.remove(cfg::history::FILE_PATH);
  LittleFS.rename(cfg::history::TEMP_FILE_PATH, cfg::history::FILE_PATH);
  _readingCount = kept;
  LOGI(TAG, "trimmed log: dropped %u, kept %u", (unsigned)skipped, (unsigned)kept);
}

bool WeatherHistoryManager::loadMeta() {
  if (!LittleFS.exists(cfg::history::META_PATH)) return false;
  File f = LittleFS.open(cfg::history::META_PATH, "r");
  if (!f) return false;

  const String countLine = f.readStringUntil('\n');
  if (countLine.length() == 0) {
    f.close();
    return false;
  }
  _readingCount = (size_t)countLine.toInt();

  _hasLastReading = false;
  if (f.available()) {
    const String readingLine = f.readStringUntil('\n');
    if (parseLine(readingLine, _lastReading)) _hasLastReading = true;
  }
  f.close();
  return true;
}

void WeatherHistoryManager::saveMeta() {
  File f = LittleFS.open(cfg::history::META_PATH, "w");
  if (!f) return;
  f.printf("%u\n", (unsigned)_readingCount);
  if (_hasLastReading) {
    char line[192];
    formatLine(_lastReading, line, sizeof(line));
    f.print(line);  // formatLine() already ends with '\n'
  }
  f.close();
}

void WeatherHistoryManager::rebuildMetaFromLog() {
  _readingCount = 0;
  _hasLastReading = false;
  File f = LittleFS.open(cfg::history::FILE_PATH, "r");
  if (f) {
    WeatherHistoryReading reading;
    while (f.available()) {
      const String line = f.readStringUntil('\n');
      if (line.length() == 0) continue;
      if (parseLine(line, reading)) {
        _readingCount++;
        _lastReading = reading;
        _hasLastReading = true;
      }
    }
    f.close();
  }
  saveMeta();
}

void WeatherHistoryManager::formatLine(const WeatherHistoryReading& reading, char* buf,
                                       size_t bufLen) {
  // Generously sized (see similar comments in WeatherService.cpp): GCC's
  // -Wformat-truncation reasons about the theoretical worst case for %f (an
  // extreme-magnitude float), not this struct's realistic value ranges, so
  // the buffer has to satisfy that analysis to stay warning-free. Callers
  // pass a 192-byte buffer, comfortably above the worst case for three %f
  // fields plus the rest of the row.
  snprintf(buf, bufLen, "%s,%.1f,%d,%.1f,%.1f,%d,%d,%d\n", reading.timestamp,
           (double)reading.temperatureC, (int)reading.humidityPct, (double)reading.pressureHpa,
           (double)reading.windSpeedKmh, (int)reading.rainProbabilityPct,
           (int)reading.cloudCoverPct, (int)reading.weatherCode);
}

bool WeatherHistoryManager::parseLine(const String& line, WeatherHistoryReading& out) {
  // A power loss mid-write can leave the last line short/truncated; sscanf
  // simply fails to match all 8 fields in that case and this returns false,
  // which every caller already treats as "skip this line" rather than an error.
  char timestamp[20];
  float temperatureC = 0.0f;
  float pressureHpa = 0.0f;
  float windSpeedKmh = 0.0f;
  int humidityPct = 0;
  int rainProbabilityPct = 0;
  int cloudCoverPct = 0;
  int weatherCode = 0;
  const int n = sscanf(line.c_str(), "%19[^,],%f,%d,%f,%f,%d,%d,%d", timestamp, &temperatureC,
                       &humidityPct, &pressureHpa, &windSpeedKmh, &rainProbabilityPct,
                       &cloudCoverPct, &weatherCode);
  if (n != 8) return false;

  strlcpy(out.timestamp, timestamp, sizeof(out.timestamp));
  out.temperatureC = temperatureC;
  out.humidityPct = (int8_t)humidityPct;
  out.pressureHpa = pressureHpa;
  out.windSpeedKmh = windSpeedKmh;
  out.rainProbabilityPct = (int8_t)rainProbabilityPct;
  out.cloudCoverPct = (int8_t)cloudCoverPct;
  out.weatherCode = (int16_t)weatherCode;
  return true;
}
