#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

#include "weather/WeatherData.h"
#include "weather/WeatherUnits.h"

// Owns the Open-Meteo fetch (current conditions + hourly forecast, one HTTP
// request) for the configured location (SettingsManager's latitude/
// longitude/locationName).
//
// The actual HTTP GET is unavoidably blocking (HTTPClient has no async
// mode), so it never runs on the main loop: requestFetch() hands off to a
// one-shot FreeRTOS task on the other core, and tick() — called cheaply and
// often from the main scheduler — just checks whether a fetch is due and
// polls whether the background task has finished. `_data` is the only state
// shared between the two contexts, guarded by `_mutex`; every other field
// touched by the background task is a snapshot copied by the main thread
// *before* the task starts (see requestFetch()), so the task never reaches
// back into SettingsManager or other main-loop-owned objects itself.
class WeatherService {
 public:
  WeatherService();
  ~WeatherService();

  WeatherService(const WeatherService&) = delete;
  WeatherService& operator=(const WeatherService&) = delete;

  void begin();

  // Call frequently (every second or so) from the main scheduler. Cheap:
  // either it's not yet time for a fetch, or a background fetch is already
  // in flight and this just returns immediately either way.
  void tick(uint32_t nowMs, bool wifiConnected);

  // Seconds between fetches; takes effect on the next due check.
  void setRefreshInterval(uint32_t seconds) { _intervalMs = seconds * 1000u; }

  // Makes the next tick() fetch immediately, regardless of the refresh
  // interval — call after a settings change that should be visible right
  // away (e.g. switching weather providers) rather than leaving the user
  // looking at stale data for up to setRefreshInterval()'s worth of minutes.
  void forceRefetch() { _everAttempted = false; }

  // Thread-safe copy of the last successfully fetched conditions. Check
  // WeatherData::valid before displaying — it's false until the first fetch
  // completes.
  WeatherData data() const;

  // Increments on every successful fetch — cheap to poll from a page's
  // tick() to decide whether a redraw is warranted, without comparing the
  // whole WeatherData struct.
  uint32_t dataVersion() const { return _version.load(); }

  // True if the most recently *completed* fetch attempt failed (network
  // down, HTTP error, bad JSON, etc). False before the first attempt
  // completes. Drives the small offline/warning indicator.
  bool lastFetchFailed() const { return _lastFetchFailed.load(); }

 private:
  static void fetchTaskTrampoline(void* arg);
  void runFetchTask();  // background task body — never touches SettingsManager
  // One of these two per fetch, chosen by _reqProvider — see
  // weather/WeatherService.cpp for why each provider needs such different
  // request/parse logic despite both filling the same WeatherData shape.
  bool runFetchOpenMeteo(WeatherData& out) const;
  bool runFetchMetNo(WeatherData& out) const;
  // Met Office Weather DataHub "Global Spot" (sitespecific/v0/point) — two
  // separate requests (hourly + daily), unlike the single combined request
  // the other providers use, because that's how this API is split; the
  // daily leg is best-effort (same reasoning as Met.no's sun-times request
  // below) since current conditions + hourly already parsed successfully by
  // the time it runs.
  bool runFetchMetOfficeDataHub(WeatherData& out) const;
  bool fetchMetOfficeDaily(float utcOffsetHours, WeatherData& out) const;
  // Best-effort sunrise/sunset from Open-Meteo — for any provider (only Met
  // Office DataHub today) whose own response has no such field at all.
  bool fetchOpenMeteoSunTimes(WeatherData& out) const;
  bool runFetchPirateWeather(WeatherData& out) const;
  bool parseOpenMeteoResponse(const String& body, WeatherData& out) const;
  bool parseMetNoResponse(const String& body, float utcOffsetHours, WeatherData& out) const;
  bool fetchMetNoSunTimes(float utcOffsetHours, WeatherData& out) const;
  bool parseMetOfficeHourlyResponse(const String& body, float utcOffsetHours,
                                    WeatherData& out) const;
  bool parseMetOfficeDailyResponse(const String& body, float utcOffsetHours,
                                   WeatherData& out) const;
  bool parsePirateWeatherResponse(const String& body, float lonForOffsetFallback,
                                  WeatherData& out) const;
  void requestFetch();

  SemaphoreHandle_t _mutex;
  WeatherData _data;
  std::atomic<bool> _fetchInFlight{false};
  std::atomic<bool> _lastFetchFailed{false};
  std::atomic<uint32_t> _version{0};
  bool _everAttempted = false;
  uint32_t _lastAttemptMs = 0;
  uint32_t _intervalMs = 0;

  // Request snapshot, written by requestFetch() (main thread) strictly
  // before the background task is created, then read-only for the task.
  float _reqLat = 0.0f;
  float _reqLon = 0.0f;
  char _reqLocationName[40] = {0};
  WeatherProvider _reqProvider = WeatherProvider::OpenMeteoAuto;
  char _reqMetOfficeApiKey[2048] = {0};
  char _reqPirateWeatherApiKey[64] = {0};
};
