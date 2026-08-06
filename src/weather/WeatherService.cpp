#include "weather/WeatherService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/task.h>

#include "config/Version.h"
#include "storage/SettingsManager.h"
#include "utils/Log.h"

static const char* TAG = "WX";

namespace {
// Exactly the fields the Current Conditions page needs — see
// weather/WeatherIcons.h and pages/CurrentConditionsPage.cpp for how each
// one is used. Requesting only these keeps the response (and the parsed
// JsonDocument) tiny.
constexpr const char* kCurrentParams =
    "temperature_2m,apparent_temperature,relative_humidity_2m,surface_pressure,"
    "wind_speed_10m,wind_direction_10m,cloud_cover,visibility,uv_index,weather_code,is_day";
// Exactly the fields the Hourly Forecast page needs (see
// pages/HourlyForecastPage.cpp and pages/PageDayHourly.cpp) — is_day drives
// day- vs night-variant icons per hour (see WeatherData.h). `forecast_hours`
// below trims the response to just this many hours instead of Open-Meteo's
// 7-day default.
constexpr const char* kHourlyParams =
    "temperature_2m,weather_code,precipitation_probability,wind_speed_10m,is_day";
// Exactly the fields the 5 Day Forecast page needs (see
// pages/ForecastPage.cpp), plus sunrise/sunset for the Sun & Moon page (see
// pages/PageSunMoon.cpp) — riding along on this same daily block rather than
// a separate request. `forecast_days` below trims both this and the
// `current=`/`hourly=` blocks to 5 days — `forecast_hours` still separately
// caps the hourly block to HOURLY_FETCH_COUNT hours within that.
constexpr const char* kDailyParams =
    "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
    "wind_speed_10m_max,sunrise,sunset";
constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr uint32_t kRequestTimeoutMs = 8000;
// Met.no is HTTPS-only; the TLS handshake routinely takes longer than a
// plain-HTTP round trip to Open-Meteo, especially on a loaded WiFi network.
constexpr uint32_t kMetNoRequestTimeoutMs = 12000;
// Pirate Weather is also HTTPS-only, and its Dark Sky-compatible payload
// (no per-field selection in the request) is considerably larger than
// Open-Meteo's or Met.no's — same generous timeout as Met.no for the TLS
// handshake, plus room for the bigger body.
constexpr uint32_t kPirateWeatherRequestTimeoutMs = 12000;
// Met Office DataHub is also HTTPS-only; same reasoning as Met.no/Pirate
// Weather above.
constexpr uint32_t kMetOfficeRequestTimeoutMs = 12000;
// mbedTLS's handshake alone can use several KB of stack on top of this
// function's own frames — the plain-HTTP-only 8192 that was enough for
// Open-Meteo was not a safe margin once WiFiClientSecure entered the mix.
constexpr uint32_t kFetchTaskStackBytes = 16384;

constexpr const char* kDayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Sakamoto's algorithm — 0=Sunday..6=Saturday for a Gregorian y-m-d.
int dayOfWeekIndex(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// `isoDate` is "YYYY-MM-DD" (Open-Meteo's daily.time entries have no time
// component, unlike current.time/hourly.time). No on-device clock exists —
// the weekday/month names are derived entirely from this string.
void formatDayLabels(const char* isoDate, char* dayLabel, size_t dayLabelSize, char* dateLabel,
                    size_t dateLabelSize) {
  int y = 0, m = 0, d = 0;
  if (isoDate == nullptr || sscanf(isoDate, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) {
    strlcpy(dayLabel, "---", dayLabelSize);
    strlcpy(dateLabel, "--- --", dateLabelSize);
    return;
  }
  strlcpy(dayLabel, kDayNames[dayOfWeekIndex(y, m, d)], dayLabelSize);
  snprintf(dateLabel, dateLabelSize, "%s %d", kMonthNames[m - 1], d);
}

// ---- Met.no support: no on-device clock, and Met.no reports everything in
// UTC, so every timestamp it returns has to be shifted by an estimated local
// offset (round(longitude/15), i.e. mean solar time — the same approximation
// already used for moonrise/moonset in pages/PageSunMoon.cpp) and the
// year/month/day rolled over by hand, since there's no library on this
// target for real timezone-aware date arithmetic. ----

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  return kDays[m - 1];
}

// Shifts a UTC y/m/d h:m by offsetHours (may be negative), rolling the date
// forward/back as needed. offsetHours is always a whole hour (round(lon/15))
// so there's no minutes-carry case to handle.
void utcToLocal(int y, int m, int d, int hh, int mi, int offsetHours, int& oy, int& om, int& od,
                int& ohh, int& omi) {
  oy = y;
  om = m;
  od = d;
  ohh = hh + offsetHours;
  omi = mi;
  while (ohh < 0) {
    ohh += 24;
    od -= 1;
    if (od < 1) {
      om -= 1;
      if (om < 1) {
        om = 12;
        oy -= 1;
      }
      od = daysInMonth(oy, om);
    }
  }
  while (ohh >= 24) {
    ohh -= 24;
    od += 1;
    if (od > daysInMonth(oy, om)) {
      od = 1;
      om += 1;
      if (om > 12) {
        om = 1;
        oy += 1;
      }
    }
  }
}

// Met.no's symbol_code (e.g. "partlycloudy_day", "lightrainshowers_night")
// has no official mapping onto WMO weather codes — this is a best-effort
// approximation so the existing WMO-code-driven icon engine (see
// weather/WeatherIcons.h) can render it unchanged. Order matters: more
// specific substrings are checked first (e.g. "heavyrain" before the generic
// "rain", and "thunder" first of all since thunder variants also contain
// "rain").
int16_t wmoCodeFromSymbol(const char* symbol) {
  if (symbol == nullptr) return -1;
  if (strstr(symbol, "thunder") != nullptr) return 95;
  if (strstr(symbol, "sleet") != nullptr) return 67;
  if (strstr(symbol, "heavysnow") != nullptr) return 75;
  if (strstr(symbol, "lightsnow") != nullptr) return 71;
  if (strstr(symbol, "snow") != nullptr) return 73;
  if (strstr(symbol, "heavyrain") != nullptr) return 65;
  if (strstr(symbol, "lightrain") != nullptr) return 61;
  if (strstr(symbol, "rain") != nullptr) return 63;
  if (strstr(symbol, "fog") != nullptr) return 45;
  if (strstr(symbol, "partlycloudy") != nullptr) return 2;
  if (strstr(symbol, "cloudy") != nullptr) return 3;
  if (strstr(symbol, "fair") != nullptr) return 1;
  if (strstr(symbol, "clearsky") != nullptr) return 0;
  return -1;
}

bool symbolIsDay(const char* symbol) {
  return symbol == nullptr || strstr(symbol, "_night") == nullptr;
}

// ---- Pirate Weather support: a Dark Sky-compatible API — see
// https://docs.pirateweather.net. Unlike Met.no, it already provides direct
// daily highs/lows/rain-probability/sunrise/sunset (no per-instant bucketing
// needed), but every timestamp is a raw Unix epoch (UTC) rather than an ISO
// string, so it needs its own epoch->local-calendar conversion below. ----

// Days-since-epoch -> proleptic Gregorian y/m/d (Howard Hinnant's
// civil_from_days: http://howardhinnant.github.io/date_algorithms.html).
// `days` may be negative (dates before 1970); not a concern here but the
// algorithm handles it for free.
void civilFromDays(long days, int& y, int& mo, int& d) {
  const long z = days + 719468L;
  const long era = (z >= 0 ? z : z - 146096L) / 146097L;
  const unsigned long doe = (unsigned long)(z - era * 146097L);
  const unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long yr = (long)yoe + era * 400;
  const unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  mo = (int)(mp + (mp < 10 ? 3 : -9));
  y = (int)(yr + (mo <= 2 ? 1 : 0));
}

// Pirate Weather's `time` fields are Unix epoch seconds, always UTC. Shifts
// by a whole-hour local offset (same whole-hour-only limitation as Met.no's
// utcToLocal above — there's no timezone-aware date library on this target)
// and breaks the result into local calendar/clock fields.
void epochToLocalParts(long epochUtc, int offsetHours, int& y, int& mo, int& d, int& hh, int& mi) {
  const long local = epochUtc + (long)offsetHours * 3600L;
  long days = local / 86400L;
  long secOfDay = local % 86400L;
  if (secOfDay < 0) {
    secOfDay += 86400L;
    days -= 1;
  }
  hh = (int)(secOfDay / 3600L);
  mi = (int)((secOfDay % 3600L) / 60L);
  civilFromDays(days, y, mo, d);
}

// Pirate Weather's `icon` (Dark Sky convention, e.g. "partly-cloudy-night",
// "clear-day") has no official WMO mapping either — same best-effort
// approach as Met.no's wmoCodeFromSymbol above. "wind" has no dedicated icon
// art in this engine (confirmed live: a windy-but-otherwise-clear day at a
// coastal test location came back as this icon on 2 of 5 days) — mapped to
// Overcast/3 rather than left as Unknown, since Dark Sky only picks "wind"
// over "clear"/"partly-cloudy" when wind is unusually prominent, which reads
// closer to "breezy/overcast" than to a clear sky.
int16_t wmoCodeFromPirateIcon(const char* icon) {
  if (icon == nullptr) return -1;
  if (strstr(icon, "sleet") != nullptr) return 67;
  if (strstr(icon, "snow") != nullptr) return 73;
  if (strstr(icon, "rain") != nullptr) return 63;
  if (strstr(icon, "fog") != nullptr) return 45;
  if (strstr(icon, "partly-cloudy") != nullptr) return 2;
  if (strstr(icon, "cloudy") != nullptr) return 3;
  if (strstr(icon, "wind") != nullptr) return 3;
  if (strstr(icon, "clear") != nullptr) return 0;
  return -1;
}

bool pirateIconIsDay(const char* icon) {
  return icon == nullptr || strstr(icon, "-night") == nullptr;
}

// ---- Met Office DataHub support: unlike the other providers, Met Office's
// `significantWeatherCode` (0-30) is an official, fully-documented table
// (https://www.metoffice.gov.uk/services/data/datapoint/code-definitions,
// unchanged from their long-standing legacy DataPoint API) rather than a
// best-effort string match — but it still needs mapping onto this app's WMO
// vocabulary since the icon engine only knows WMO codes. Only codes 0-3 have
// distinct night/day variants (matching this app's icon engine, which also
// only varies Clear/PartlyCloudy by isDay) — every other code applies
// equally at any hour. ----
int16_t wmoCodeFromMetOfficeCode(int16_t code) {
  switch (code) {
    case 0:
      return 0;  // Clear night
    case 1:
      return 1;  // Sunny day
    case 2:
    case 3:
      return 2;  // Partly cloudy (night/day)
    case 5:
    case 6:
      return 45;  // Mist, Fog
    case 7:
    case 8:
      return 3;  // Cloudy, Overcast — this engine has one generic cloud icon
    case 9:
    case 10:
      return 80;  // Light rain shower (night/day)
    case 11:
      return 51;  // Drizzle
    case 12:
      return 61;  // Light rain
    case 13:
    case 14:
      return 82;  // Heavy rain shower (night/day)
    case 15:
      return 65;  // Heavy rain
    case 16:
    case 17:
    case 18:
      return 67;  // Sleet shower (night/day), sleet — no dedicated sleet
                  // icon exists here either, same fallback as Met.no/Pirate
                  // Weather's sleet handling above
    case 19:
    case 20:
    case 21:
      return 82;  // Hail (shower night/day, or plain) — no hail icon exists;
                  // closest visual match is heavy rain, same reasoning as
                  // "wind" above
    case 22:
    case 23:
      return 85;  // Light snow shower (night/day)
    case 24:
      return 71;  // Light snow
    case 25:
    case 26:
      return 86;  // Heavy snow shower (night/day)
    case 27:
      return 75;  // Heavy snow
    case 28:
    case 29:
    case 30:
      return 95;  // Thunder shower (night/day), thunder
    default:
      return -1;  // includes 4 ("Not used") and anything unrecognized
  }
}

bool metOfficeCodeIsDay(int16_t code) {
  switch (code) {
    case 0:
    case 2:
    case 9:
    case 13:
    case 16:
    case 19:
    case 22:
    case 25:
    case 28:
      return false;  // the "(night)" variant of each night/day pair
    default:
      return true;
  }
}

// Aggregation state for one local calendar day, built up while walking
// Met.no's flat timeseries array — see parseMetNoResponse's daily loop.
struct MetNoDayBucket {
  int y = 0, m = 0, d = 0;
  float highC = -1000.0f;
  float lowC = 1000.0f;
  float windMaxKmh = 0.0f;
  bool rainLikely = false;
  const char* midDaySymbol = nullptr;
  int bestHourDist = 999;  // smallest abs(localHour - 12) seen so far
};
}  // namespace

WeatherService::WeatherService() {
  _mutex = xSemaphoreCreateMutex();
}

WeatherService::~WeatherService() {
  if (_mutex != nullptr) vSemaphoreDelete(_mutex);
}

void WeatherService::begin() {
  _everAttempted = false;
  _lastAttemptMs = 0;
}

void WeatherService::tick(uint32_t nowMs, bool wifiConnected) {
  if (_fetchInFlight.load()) return;  // background task owns everything until it clears this
  if (!wifiConnected) return;
  const bool due = !_everAttempted || (nowMs - _lastAttemptMs >= _intervalMs);
  if (!due) return;
  _everAttempted = true;
  _lastAttemptMs = nowMs;
  requestFetch();
}

void WeatherService::requestFetch() {
  const AppSettings& settings = SettingsManager::instance().get();
  _reqLat = settings.latitude;
  _reqLon = settings.longitude;
  strlcpy(_reqLocationName, settings.locationName, sizeof(_reqLocationName));
  _reqProvider = (WeatherProvider)settings.weatherProvider;
  strlcpy(_reqMetOfficeApiKey, settings.metOfficeApiKey, sizeof(_reqMetOfficeApiKey));
  strlcpy(_reqPirateWeatherApiKey, settings.pirateWeatherApiKey, sizeof(_reqPirateWeatherApiKey));

  _fetchInFlight.store(true);
  // Pinned to core 0 (the WiFi/BT protocol core) — Arduino's own loopTask,
  // which runs our scheduler (render/touch/page-rotation), lives on core 1
  // by default. Different cores means this blocking HTTP call genuinely
  // never steals CPU time from the UI loop, rather than merely time-slicing
  // against it on the same core.
  const BaseType_t ok = xTaskCreatePinnedToCore(&WeatherService::fetchTaskTrampoline, "wxFetch",
                                                kFetchTaskStackBytes, this,
                                                /*priority=*/1, nullptr, /*core=*/0);
  if (ok != pdPASS) {
    LOGE(TAG, "could not start fetch task (out of memory?)");
    _fetchInFlight.store(false);
    _lastFetchFailed.store(true);
  }
}

void WeatherService::fetchTaskTrampoline(void* arg) {
  static_cast<WeatherService*>(arg)->runFetchTask();
  vTaskDelete(nullptr);
}

void WeatherService::runFetchTask() {
  WeatherData fresh;
  bool ok;
  switch (_reqProvider) {
    case WeatherProvider::MetNo:
      ok = runFetchMetNo(fresh);
      break;
    case WeatherProvider::MetOfficeDataHub:
      ok = runFetchMetOfficeDataHub(fresh);
      break;
    case WeatherProvider::PirateWeather:
      ok = runFetchPirateWeather(fresh);
      break;
    default:
      ok = runFetchOpenMeteo(fresh);
      break;
  }

  if (ok) {
    strlcpy(fresh.locationName, _reqLocationName, sizeof(fresh.locationName));
    fresh.valid = true;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _data = fresh;
    xSemaphoreGive(_mutex);
    _version.fetch_add(1);
    LOGI(TAG, "fetched: %.1fC %s", (double)fresh.temperatureC, fresh.lastUpdate);
  }
  _lastFetchFailed.store(!ok);
  _fetchInFlight.store(false);  // must be last: tells the main loop it may fetch again
}

bool WeatherService::runFetchOpenMeteo(WeatherData& out) const {
  // Sized well past any realistic URL (lat/lon are single-digit-precision
  // degrees) — GCC's -Wformat-truncation reasons about the theoretical worst
  // case for %f (an extreme-magnitude double), not our actual -90..90/
  // -180..180 range, so the buffer has to satisfy that analysis to stay
  // warning-free.
  char url[896];
  const int n = snprintf(
      url, sizeof(url),
      "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=%s&"
      "hourly=%s&forecast_hours=%u&daily=%s&forecast_days=%u&timezone=auto",
      (double)_reqLat, (double)_reqLon, kCurrentParams, kHourlyParams,
      (unsigned)cfg::weather::HOURLY_FETCH_COUNT, kDailyParams,
      (unsigned)cfg::weather::DAILY_FETCH_COUNT);
  // Pins the forecast to one national weather model instead of Open-Meteo's
  // default best-match blend — same free, keyless endpoint and response
  // schema for every model, so nothing else about this fetch/parse changes.
  // Most of these models are regional (e.g. UKV is UK/Western-Europe-only).
  // Confirmed live (2026-08-02): outside a model's coverage Open-Meteo does
  // NOT fall back to another model — it returns the request as HTTP 200 with
  // `current`/`hourly`/`daily` fields present but null (e.g. bom_access_global
  // for a UK location). This parser's existing safe-fallback field reads
  // (`current["x"] | 0.0f` etc., see parseOpenMeteoResponse below) turn that
  // into a "successful" fetch of all-zero data rather than a failure — same
  // accepted tradeoff as Met.no's missing UV/visibility fields. Selecting a
  // model with no coverage for the configured location is a silent no-data
  // state, not a fetch error.
  const char* modelSlug = openMeteoModelSlug(_reqProvider);
  if (modelSlug != nullptr && n > 0 && (size_t)n < sizeof(url)) {
    strlcat(url, "&models=", sizeof(url));
    strlcat(url, modelSlug, sizeof(url));
  }

  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kRequestTimeoutMs);

  bool ok = false;
  if (http.begin(url)) {
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      // Open-Meteo replies with Transfer-Encoding: chunked (no
      // Content-Length) — getStream() exposes the *raw* chunked bytes
      // (chunk-size headers included), which corrupts a direct parse.
      // getString() de-chunks internally, so parse from that instead; the
      // body is only ~1-2 KB, trivial to hold as a String for the moment it
      // takes to parse.
      ok = parseOpenMeteoResponse(http.getString(), out);
    } else {
      LOGW(TAG, "Open-Meteo fetch failed: HTTP %d", code);
    }
    http.end();
  } else {
    LOGW(TAG, "Open-Meteo fetch failed: could not open connection");
  }
  return ok;
}

// User-Agent Met.no requires on every request (undocumented-403 otherwise) —
// built from the firmware's own name/version since there's no project URL to
// point at.
static void metNoUserAgent(char* out, size_t outSize) {
  snprintf(out, outSize, "%s/%s (personal ESP32 weather display)", FW_NAME, FW_VERSION);
}

bool WeatherService::runFetchMetNo(WeatherData& out) const {
  const float utcOffsetHours = roundf(_reqLon / 15.0f);

  char url[160];
  snprintf(url, sizeof(url),
           "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=%.4f&lon=%.4f",
           (double)_reqLat, (double)_reqLon);

  char userAgent[80];
  metNoUserAgent(userAgent, sizeof(userAgent));

  // .setInsecure() skips certificate validation — a deliberate simplification
  // for a hobbyist device fetching public, non-sensitive weather data; it
  // avoids having to embed and maintain a CA bundle just for this one host.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kMetNoRequestTimeoutMs);

  bool ok = false;
  if (http.begin(client, url)) {
    http.addHeader("User-Agent", userAgent);
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      ok = parseMetNoResponse(http.getString(), utcOffsetHours, out);
    } else {
      LOGW(TAG, "Met.no forecast fetch failed: HTTP %d", code);
    }
    http.end();
  } else {
    LOGW(TAG, "Met.no forecast fetch failed: could not open connection");
  }

  if (ok) {
    // Best-effort, separate request — a failure here doesn't invalidate the
    // forecast that was already parsed above.
    strlcpy(out.sunriseTime, "--:--", sizeof(out.sunriseTime));
    strlcpy(out.sunsetTime, "--:--", sizeof(out.sunsetTime));
    if (!fetchMetNoSunTimes(utcOffsetHours, out)) {
      LOGW(TAG, "Met.no sunrise/sunset fetch failed (forecast still used)");
    }
  }
  return ok;
}

// Endpoint, headers and query params confirmed against the Met Office's own
// reference client (github.com/MetOffice/weather_datahub_utilities,
// site_specific_download/ss_download.py) — the interactive docs page is
// JS-rendered and never returned this directly. excludeParameterMetadata
// drops the (large, static-per-request) per-field description block this
// struct never reads; includeLocationName=false drops a location name this
// app already has from its own settings.
bool WeatherService::runFetchMetOfficeDataHub(WeatherData& out) const {
  if (_reqMetOfficeApiKey[0] == '\0') {
    LOGW(TAG, "Met Office DataHub: no API key configured (Settings -> Weather data source)");
    return false;
  }

  const float utcOffsetHours = roundf(_reqLon / 15.0f);

  char url[256];
  snprintf(url, sizeof(url),
           "https://data.hub.api.metoffice.gov.uk/sitespecific/v0/point/hourly"
           "?excludeParameterMetadata=true&includeLocationName=false&latitude=%.4f&longitude=%.4f",
           (double)_reqLat, (double)_reqLon);

  // .setInsecure() skips certificate validation — same deliberate
  // simplification as Met.no/Pirate Weather above, for the same reason.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kMetOfficeRequestTimeoutMs);

  bool ok = false;
  if (http.begin(client, url)) {
    http.addHeader("apikey", _reqMetOfficeApiKey);
    http.addHeader("accept", "application/json");
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      ok = parseMetOfficeHourlyResponse(http.getString(), utcOffsetHours, out);
    } else {
      // Body included (unlike the other providers' failure logs) — WSO2's
      // auth-failure JSON (e.g. "Invalid Credentials") is far more
      // actionable than the bare status code alone, and it's always small.
      LOGW(TAG, "Met Office DataHub hourly fetch failed: HTTP %d, body: %s", code,
           http.getString().c_str());
    }
    http.end();
  } else {
    LOGW(TAG, "Met Office DataHub hourly fetch failed: could not open connection");
  }

  if (ok) {
    // Best-effort, separate requests — neither failure invalidates the
    // current conditions/hourly data already parsed above.
    if (!fetchMetOfficeDaily(utcOffsetHours, out)) {
      LOGW(TAG, "Met Office DataHub daily fetch failed (hourly data still used)");
    }
    // Neither Met Office response has a sunrise/sunset field at all (unlike
    // Open-Meteo/Met.no/Pirate Weather) — confirmed live (PageSunMoon just
    // showed "no data" for the Sun card). Rather than approximate it
    // on-device the way the Moon's phase/rise/set already are (no API
    // equivalent exists for those), borrow Open-Meteo's real sunrise/sunset
    // for this same location — free, keyless, and already the pattern this
    // app uses elsewhere for supplementary best-effort data (see Met.no's
    // fetchMetNoSunTimes above).
    strlcpy(out.sunriseTime, "--:--", sizeof(out.sunriseTime));
    strlcpy(out.sunsetTime, "--:--", sizeof(out.sunsetTime));
    if (!fetchOpenMeteoSunTimes(out)) {
      LOGW(TAG, "Met Office DataHub: Open-Meteo sun-times fallback failed (forecast still used)");
    }
  }
  return ok;
}

bool WeatherService::fetchMetOfficeDaily(float utcOffsetHours, WeatherData& out) const {
  char url[256];
  snprintf(url, sizeof(url),
           "https://data.hub.api.metoffice.gov.uk/sitespecific/v0/point/daily"
           "?excludeParameterMetadata=true&includeLocationName=false&latitude=%.4f&longitude=%.4f",
           (double)_reqLat, (double)_reqLon);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kMetOfficeRequestTimeoutMs);

  bool ok = false;
  if (http.begin(client, url)) {
    http.addHeader("apikey", _reqMetOfficeApiKey);
    http.addHeader("accept", "application/json");
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      ok = parseMetOfficeDailyResponse(http.getString(), utcOffsetHours, out);
    } else {
      LOGW(TAG, "Met Office DataHub daily fetch failed: HTTP %d", code);
    }
    http.end();
  } else {
    LOGW(TAG, "Met Office DataHub daily fetch failed: could not open connection");
  }
  return ok;
}

// Plain HTTP, same as runFetchOpenMeteo — no TLS needed for this host.
// forecast_days=1 and a filter that keeps only daily.sunrise/sunset make
// this a tiny request; timezone=auto gets Open-Meteo's own (real,
// DST-aware) local-time conversion, which is actually *more* accurate than
// this app's usual estimated-UTC-offset fallback for providers that lack
// their own timezone handling (see Met.no's utcToLocal usage elsewhere in
// this file) — a nice side effect of borrowing this endpoint rather than
// computing sun times on-device.
bool WeatherService::fetchOpenMeteoSunTimes(WeatherData& out) const {
  char url[160];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&"
           "daily=sunrise,sunset&forecast_days=1&timezone=auto",
           (double)_reqLat, (double)_reqLon);

  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kRequestTimeoutMs);

  bool ok = false;
  if (http.begin(url)) {
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      JsonDocument filter;
      filter["daily"]["sunrise"] = true;
      filter["daily"]["sunset"] = true;
      JsonDocument doc;
      const DeserializationError err =
          deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
      if (!err) {
        JsonArrayConst sunrise = doc["daily"]["sunrise"].as<JsonArrayConst>();
        JsonArrayConst sunset = doc["daily"]["sunset"].as<JsonArrayConst>();
        const char* sr = sunrise.size() > 0 ? sunrise[0].as<const char*>() : nullptr;
        const char* ss = sunset.size() > 0 ? sunset[0].as<const char*>() : nullptr;
        if (sr != nullptr) {
          const char* c = strchr(sr, 'T');
          strlcpy(out.sunriseTime, c != nullptr ? c + 1 : sr, sizeof(out.sunriseTime));
        }
        if (ss != nullptr) {
          const char* c = strchr(ss, 'T');
          strlcpy(out.sunsetTime, c != nullptr ? c + 1 : ss, sizeof(out.sunsetTime));
        }
        ok = (sr != nullptr) && (ss != nullptr);
      } else {
        LOGW(TAG, "Open-Meteo sun-times JSON parse failed: %s", err.c_str());
      }
    } else {
      LOGW(TAG, "Open-Meteo sun-times fetch failed: HTTP %d", code);
    }
    http.end();
  } else {
    LOGW(TAG, "Open-Meteo sun-times fetch failed: could not open connection");
  }
  return ok;
}

bool WeatherService::runFetchPirateWeather(WeatherData& out) const {
  if (_reqPirateWeatherApiKey[0] == '\0') {
    LOGW(TAG, "Pirate Weather: no API key configured (Settings -> Weather data source)");
    return false;
  }

  // units=si gives Celsius/m/s/hPa/km, matching (or one multiply away from,
  // for wind) WeatherData's storage units. exclude= drops blocks this
  // struct never uses. Deliberately *not* requesting extend=hourly (the
  // full multi-day hourly block): confirmed live that it inflates the body
  // past 85 KB, and HTTPClient::getString() needs one contiguous heap
  // allocation that size — it silently returned "" here even with >100 KB
  // of *total* free heap (fragmentation), which read as a JSON parse
  // failure further down. Pirate Weather's default (unextended) hourly
  // window is far smaller and comfortably fits a single allocation, at the
  // cost of the Hourly Forecast/5-Day-drilldown pages having less hourly
  // depth to work with than Open-Meteo gives them for the same
  // HOURLY_FETCH_COUNT — those pages already treat a short hourly array as
  // best-effort (see out.hourlyCount below).
  char url[192];
  snprintf(url, sizeof(url), "https://api.pirateweather.net/forecast/%s/%.4f,%.4f"
                              "?units=si&exclude=minutely,alerts,flags",
           _reqPirateWeatherApiKey, (double)_reqLat, (double)_reqLon);

  // .setInsecure() skips certificate validation — same deliberate
  // simplification as Met.no above, for the same reason (hobbyist device,
  // public non-sensitive data, avoids maintaining a CA bundle).
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kPirateWeatherRequestTimeoutMs);

  bool ok = false;
  if (http.begin(client, url)) {
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      ok = parsePirateWeatherResponse(http.getString(), _reqLon, out);
    } else {
      LOGW(TAG, "Pirate Weather fetch failed: HTTP %d", code);
    }
    http.end();
  } else {
    LOGW(TAG, "Pirate Weather fetch failed: could not open connection");
  }
  return ok;
}

bool WeatherService::fetchMetNoSunTimes(float utcOffsetHoursF, WeatherData& out) const {
  // out.isoTime was just filled in by parseMetNoResponse as
  // "YYYY-MM-DDTHH:MM" (already shifted to local time) — its first 10
  // characters are exactly the local calendar date the sunrise/sunset
  // endpoint needs.
  char dateStr[11];
  strlcpy(dateStr, out.isoTime, sizeof(dateStr));

  const int offsetHours = (int)lroundf(utcOffsetHoursF);
  // Oversized vs. the realistic -12..14 range so -Wformat-truncation (which
  // reasons about %d's theoretical INT_MIN/MAX width, not our actual range)
  // stays quiet — see the kCurrentParams-buffer comment above for the same
  // pattern.
  char offsetStr[16];
  snprintf(offsetStr, sizeof(offsetStr), "%c%02d:00", offsetHours >= 0 ? '+' : '-',
           abs(offsetHours));

  char url[192];
  snprintf(url, sizeof(url),
           "https://api.met.no/weatherapi/sunrise/3.0/sun?lat=%.4f&lon=%.4f&date=%s&offset=%s",
           (double)_reqLat, (double)_reqLon, dateStr, offsetStr);

  char userAgent[80];
  metNoUserAgent(userAgent, sizeof(userAgent));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout((int32_t)kConnectTimeoutMs);
  http.setTimeout((uint16_t)kMetNoRequestTimeoutMs);

  bool ok = false;
  if (http.begin(client, url)) {
    http.addHeader("User-Agent", userAgent);
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      JsonDocument filter;
      filter["properties"]["sunrise"]["time"] = true;
      filter["properties"]["sunset"]["time"] = true;
      JsonDocument doc;
      const DeserializationError err =
          deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
      if (!err) {
        // The `offset=` query param makes these times come back already
        // shifted to local wall-clock time — same "grab HH:MM after the T"
        // handling as every other timestamp in this file.
        const char* sr = doc["properties"]["sunrise"]["time"];
        const char* ss = doc["properties"]["sunset"]["time"];
        if (sr != nullptr) {
          const char* c = strchr(sr, 'T');
          strlcpy(out.sunriseTime, c != nullptr ? c + 1 : sr, sizeof(out.sunriseTime));
        }
        if (ss != nullptr) {
          const char* c = strchr(ss, 'T');
          strlcpy(out.sunsetTime, c != nullptr ? c + 1 : ss, sizeof(out.sunsetTime));
        }
        ok = (sr != nullptr) && (ss != nullptr);
      } else {
        LOGW(TAG, "Met.no sun JSON parse failed: %s", err.c_str());
      }
    } else {
      LOGW(TAG, "Met.no sun fetch failed: HTTP %d", code);
    }
    http.end();
  } else {
    LOGW(TAG, "Met.no sun fetch failed: could not open connection");
  }
  return ok;
}

bool WeatherService::parseOpenMeteoResponse(const String& body, WeatherData& out) const {
  // Filtering keeps the parser from ever materializing the fields we don't
  // want (latitude/longitude/timezone/current_units/...), so only the small
  // handful of values this struct actually stores ends up in `doc` below.
  JsonDocument filter;
  filter["current"]["time"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["apparent_temperature"] = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["current"]["surface_pressure"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["current"]["wind_direction_10m"] = true;
  filter["current"]["cloud_cover"] = true;
  filter["current"]["visibility"] = true;
  filter["current"]["uv_index"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["hourly"]["time"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["weather_code"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["hourly"]["wind_speed_10m"] = true;
  filter["hourly"]["is_day"] = true;
  filter["daily"]["time"] = true;
  filter["daily"]["weather_code"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  filter["daily"]["precipitation_probability_max"] = true;
  filter["daily"]["wind_speed_10m_max"] = true;
  filter["daily"]["sunrise"] = true;
  filter["daily"]["sunset"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    LOGW(TAG, "JSON parse failed: %s", err.c_str());
    return false;
  }

  JsonObjectConst current = doc["current"].as<JsonObjectConst>();
  const char* time = current["time"];
  if (current.isNull() || time == nullptr) {
    LOGW(TAG, "JSON missing 'current' block");
    return false;
  }

  // Every field below is read with a safe fallback — a partial/malformed
  // payload still yields usable (if incomplete) data rather than failing
  // outright or reading garbage.
  out.temperatureC = current["temperature_2m"] | 0.0f;
  out.feelsLikeC = current["apparent_temperature"] | out.temperatureC;
  out.humidityPct = current["relative_humidity_2m"] | 0;
  out.pressureHpa = current["surface_pressure"] | 0.0f;
  out.windSpeedKmh = current["wind_speed_10m"] | 0.0f;
  out.windDirectionDeg = current["wind_direction_10m"] | 0;
  out.cloudCoverPct = current["cloud_cover"] | 0;
  out.visibilityKm = (current["visibility"] | 0.0f) / 1000.0f;  // Open-Meteo reports metres
  out.uvIndex = current["uv_index"] | 0.0f;
  out.weatherCode = current["weather_code"] | (int16_t)-1;
  out.isDay = (current["is_day"] | 1) != 0;

  // current.time is ISO8601, e.g. "2026-07-31T14:30". lastUpdate keeps just
  // "HH:MM" for display; isoTime keeps the whole thing as the closest thing
  // to a real timestamp this clockless device has (see
  // storage/WeatherHistoryManager.h).
  strlcpy(out.isoTime, time, sizeof(out.isoTime));
  const char* clock = strchr(time, 'T');
  strlcpy(out.lastUpdate, clock != nullptr ? clock + 1 : "--:--", sizeof(out.lastUpdate));

  // Per-day "closest to local midday" weather code, computed while walking
  // the hourly block below — used to override the daily block's own
  // weather_code further down. Open-Meteo's daily weather_code picks the
  // single most-significant condition anywhere in the 24h window, so one
  // drizzly hour at 11pm outweighs 23 clear hours and paints the whole day's
  // icon as rain — confirmed live (2026-08-03: every hour 00:00-22:00 was
  // clear/partly-cloudy, only 23:00 was drizzle, and Open-Meteo's own daily
  // code for that date was "53 - moderate drizzle"). Picking the hour
  // closest to noon instead reads as "roughly what the day looked like",
  // and matches Met.no's identical midday-nearest heuristic for its own
  // (self-computed) daily bucketing — see parseMetNoResponse below — so
  // both providers' 5 Day Forecast cards behave consistently.
  int16_t middayCode[cfg::weather::DAILY_FETCH_COUNT];
  uint8_t middayDist[cfg::weather::DAILY_FETCH_COUNT];
  for (uint8_t i = 0; i < cfg::weather::DAILY_FETCH_COUNT; i++) {
    middayCode[i] = -1;
    middayDist[i] = 255;
  }

  // The hourly block is best-effort: current conditions above already parsed
  // successfully, so a missing/short/malformed hourly array just leaves
  // hourlyCount at 0 rather than failing the whole fetch (Hourly Forecast
  // page falls back to its own "no data" state — see WeatherData::hourlyCount).
  JsonObjectConst hourly = doc["hourly"].as<JsonObjectConst>();
  if (!hourly.isNull()) {
    JsonArrayConst times = hourly["time"].as<JsonArrayConst>();
    JsonArrayConst temps = hourly["temperature_2m"].as<JsonArrayConst>();
    JsonArrayConst codes = hourly["weather_code"].as<JsonArrayConst>();
    JsonArrayConst rain = hourly["precipitation_probability"].as<JsonArrayConst>();
    JsonArrayConst wind = hourly["wind_speed_10m"].as<JsonArrayConst>();
    JsonArrayConst isDay = hourly["is_day"].as<JsonArrayConst>();

    const size_t n = min((size_t)times.size(), (size_t)cfg::weather::HOURLY_FETCH_COUNT);
    uint8_t dayIdx = 0;
    for (size_t i = 0; i < n; i++) {
      HourlyForecastEntry& e = out.hourly[i];
      const char* hourTime = times[i];
      const char* hourClock = hourTime != nullptr ? strchr(hourTime, 'T') : nullptr;
      strlcpy(e.time, hourClock != nullptr ? hourClock + 1 : "--:--", sizeof(e.time));
      e.temperatureC = temps[i] | 0.0f;
      e.weatherCode = codes[i] | (int16_t)-1;
      e.rainProbabilityPct = rain[i] | 0;
      e.windSpeedKmh = wind[i] | 0.0f;
      e.isDay = (isDay[i] | 1) != 0;

      int hh = 0, mm = 0;
      if (sscanf(e.time, "%d:%d", &hh, &mm) == 2) {
        if (i > 0 && hh == 0 && mm == 0) dayIdx++;  // crossed a day boundary
        if (dayIdx < cfg::weather::DAILY_FETCH_COUNT) {
          const uint8_t dist = (uint8_t)abs(hh - 12);
          if (dist < middayDist[dayIdx]) {
            middayDist[dayIdx] = dist;
            middayCode[dayIdx] = e.weatherCode;
          }
        }
      }
    }
    out.hourlyCount = (uint8_t)n;
  }

  // Best-effort, same reasoning as the hourly block above.
  JsonObjectConst daily = doc["daily"].as<JsonObjectConst>();
  if (!daily.isNull()) {
    JsonArrayConst dates = daily["time"].as<JsonArrayConst>();
    JsonArrayConst codes = daily["weather_code"].as<JsonArrayConst>();
    JsonArrayConst highs = daily["temperature_2m_max"].as<JsonArrayConst>();
    JsonArrayConst lows = daily["temperature_2m_min"].as<JsonArrayConst>();
    JsonArrayConst rain = daily["precipitation_probability_max"].as<JsonArrayConst>();
    JsonArrayConst wind = daily["wind_speed_10m_max"].as<JsonArrayConst>();
    JsonArrayConst sunrise = daily["sunrise"].as<JsonArrayConst>();
    JsonArrayConst sunset = daily["sunset"].as<JsonArrayConst>();

    const size_t n = min((size_t)dates.size(), (size_t)cfg::weather::DAILY_FETCH_COUNT);
    for (size_t i = 0; i < n; i++) {
      DailyForecastEntry& e = out.daily[i];
      const char* isoDate = dates[i];
      formatDayLabels(isoDate, e.dayLabel, sizeof(e.dayLabel), e.dateLabel, sizeof(e.dateLabel));
      e.weatherCode = middayCode[i] != -1 ? middayCode[i] : (int16_t)(codes[i] | (int16_t)-1);
      e.highC = highs[i] | 0.0f;
      e.lowC = lows[i] | 0.0f;
      e.rainProbabilityPct = rain[i] | 0;
      e.windSpeedMaxKmh = wind[i] | 0.0f;
    }
    out.dailyCount = (uint8_t)n;

    // Sun & Moon page only ever needs *today's* sunrise/sunset (index 0) —
    // "HH:MM" only, same convention as lastUpdate above.
    if (n > 0) {
      const char* sr = sunrise[0];
      const char* srClock = sr != nullptr ? strchr(sr, 'T') : nullptr;
      strlcpy(out.sunriseTime, srClock != nullptr ? srClock + 1 : "--:--", sizeof(out.sunriseTime));
      const char* ss = sunset[0];
      const char* ssClock = ss != nullptr ? strchr(ss, 'T') : nullptr;
      strlcpy(out.sunsetTime, ssClock != nullptr ? ssClock + 1 : "--:--", sizeof(out.sunsetTime));
    }
  }

  return true;
}

bool WeatherService::parseMetNoResponse(const String& body, float utcOffsetHoursF,
                                        WeatherData& out) const {
  const int offsetHours = (int)lroundf(utcOffsetHoursF);

  // The `[0]` template below is ArduinoJson's convention for "apply this
  // shape to every element" of the timeseries array, not just index 0 — see
  // https://arduinojson.org/news/2021/05/28/version-6-18-0/#filtering. Every
  // entry gets the same small set of fields; entries beyond the first ~2.5
  // days simply won't have next_1_hours (see the hourly loop below).
  JsonDocument filter;
  filter["properties"]["timeseries"][0]["time"] = true;
  JsonObject instantFilter =
      filter["properties"]["timeseries"][0]["data"]["instant"]["details"].to<JsonObject>();
  instantFilter["air_temperature"] = true;
  instantFilter["relative_humidity"] = true;
  instantFilter["air_pressure_at_sea_level"] = true;
  instantFilter["wind_speed"] = true;
  instantFilter["wind_from_direction"] = true;
  instantFilter["cloud_area_fraction"] = true;
  filter["properties"]["timeseries"][0]["data"]["next_1_hours"]["summary"]["symbol_code"] = true;
  filter["properties"]["timeseries"][0]["data"]["next_1_hours"]["details"]["precipitation_amount"] =
      true;
  filter["properties"]["timeseries"][0]["data"]["next_6_hours"]["summary"]["symbol_code"] = true;
  filter["properties"]["timeseries"][0]["data"]["next_6_hours"]["details"]["precipitation_amount"] =
      true;
  filter["properties"]["timeseries"][0]["data"]["next_12_hours"]["summary"]["symbol_code"] = true;
  filter["properties"]["timeseries"][0]["data"]["next_12_hours"]["details"]["precipitation_amount"] =
      true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    LOGW(TAG, "Met.no JSON parse failed: %s", err.c_str());
    return false;
  }

  JsonArrayConst series = doc["properties"]["timeseries"].as<JsonArrayConst>();
  if (series.isNull() || series.size() == 0) {
    LOGW(TAG, "Met.no JSON missing 'timeseries'");
    return false;
  }

  // --- current conditions: the first (soonest) entry ---
  JsonObjectConst first = series[0].as<JsonObjectConst>();
  const char* isoUtc = first["time"];
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
  if (isoUtc == nullptr || sscanf(isoUtc, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &hh, &mi, &ss) < 5) {
    LOGW(TAG, "Met.no JSON missing/malformed timeseries[0].time");
    return false;
  }
  int ly = 0, lmo = 0, ld = 0, lhh = 0, lmi = 0;
  utcToLocal(y, mo, d, hh, mi, offsetHours, ly, lmo, ld, lhh, lmi);
  snprintf(out.isoTime, sizeof(out.isoTime), "%04d-%02d-%02dT%02d:%02d", ly, lmo, ld, lhh, lmi);
  snprintf(out.lastUpdate, sizeof(out.lastUpdate), "%02d:%02d", lhh, lmi);

  JsonObjectConst instant = first["data"]["instant"]["details"].as<JsonObjectConst>();
  out.temperatureC = instant["air_temperature"] | 0.0f;
  out.feelsLikeC = out.temperatureC;  // locationforecast/2.0 has no apparent-temperature field
  out.humidityPct = (int8_t)(instant["relative_humidity"] | 0.0f);
  out.pressureHpa = instant["air_pressure_at_sea_level"] | 0.0f;
  out.windSpeedKmh = (instant["wind_speed"] | 0.0f) * 3.6f;  // Met.no reports m/s
  out.windDirectionDeg = (int16_t)(instant["wind_from_direction"] | 0.0f);
  out.cloudCoverPct = (int8_t)(instant["cloud_area_fraction"] | 0.0f);
  out.visibilityKm = 0.0f;  // not provided by this endpoint
  out.uvIndex = 0.0f;       // not provided by this endpoint

  JsonObjectConst n1 = first["data"]["next_1_hours"].as<JsonObjectConst>();
  JsonObjectConst n6 = first["data"]["next_6_hours"].as<JsonObjectConst>();
  JsonObjectConst n12 = first["data"]["next_12_hours"].as<JsonObjectConst>();
  const char* symbol = nullptr;
  if (!n1.isNull()) symbol = n1["summary"]["symbol_code"] | (const char*)nullptr;
  if (symbol == nullptr && !n6.isNull()) symbol = n6["summary"]["symbol_code"] | (const char*)nullptr;
  if (symbol == nullptr && !n12.isNull()) symbol = n12["summary"]["symbol_code"] | (const char*)nullptr;
  out.weatherCode = wmoCodeFromSymbol(symbol);
  out.isDay = symbolIsDay(symbol);

  // --- hourly forecast ---
  // next_1_hours only covers the first ~2.5 days (confirmed live: it runs
  // out mid-day, not on a day boundary) — entries beyond that still exist,
  // just with next_6_hours or next_12_hours instead. Skipping those entries
  // entirely (as this used to) truncated whichever day the cutoff fell in
  // and dropped every later day completely — the "Tuesday just sits at
  // 00:00" bug. Falling back through en1 -> en6 -> en12 (same chain already
  // used for current conditions and the daily bucketing below) keeps every
  // day populated, just at coarser resolution once the finest data runs out.
  uint8_t hIdx = 0;
  for (size_t i = 0; i < series.size() && hIdx < cfg::weather::HOURLY_FETCH_COUNT; i++) {
    JsonObjectConst e = series[i].as<JsonObjectConst>();
    JsonObjectConst en1 = e["data"]["next_1_hours"].as<JsonObjectConst>();
    JsonObjectConst en6 = e["data"]["next_6_hours"].as<JsonObjectConst>();
    JsonObjectConst en12 = e["data"]["next_12_hours"].as<JsonObjectConst>();
    JsonObjectConst chosen = !en1.isNull() ? en1 : (!en6.isNull() ? en6 : en12);
    if (chosen.isNull()) continue;  // no forecast summary at all for this entry (the far end)
    const char* t = e["time"];
    int ey = 0, emo = 0, ed = 0, ehh = 0, emi = 0, ess = 0;
    if (t == nullptr || sscanf(t, "%d-%d-%dT%d:%d:%d", &ey, &emo, &ed, &ehh, &emi, &ess) < 5) {
      continue;
    }
    int lly = 0, llmo = 0, lld = 0, llhh = 0, llmi = 0;
    utcToLocal(ey, emo, ed, ehh, emi, offsetHours, lly, llmo, lld, llhh, llmi);

    HourlyForecastEntry& he = out.hourly[hIdx];
    snprintf(he.time, sizeof(he.time), "%02d:%02d", llhh, llmi);
    JsonObjectConst einstant = e["data"]["instant"]["details"].as<JsonObjectConst>();
    he.temperatureC = einstant["air_temperature"] | 0.0f;
    const char* hourSymbol = chosen["summary"]["symbol_code"] | (const char*)nullptr;
    he.weatherCode = wmoCodeFromSymbol(hourSymbol);
    he.isDay = symbolIsDay(hourSymbol);
    // locationforecast/2.0 has no precipitation-probability field, only a
    // forecast precipitation amount — used here as a coarse stand-in
    // (any measurable rain forecast counts as "likely"), not a calibrated
    // probability like Open-Meteo's.
    const float precipMm = chosen["details"]["precipitation_amount"] | 0.0f;
    he.rainProbabilityPct = precipMm > 0.1f ? 60 : 0;
    he.windSpeedKmh = (einstant["wind_speed"] | 0.0f) * 3.6f;
    hIdx++;
  }
  out.hourlyCount = hIdx;

  // --- daily forecast: bucket the same timeseries by local calendar date.
  // locationforecast/2.0 has no daily-aggregate endpoint at all — highs/lows
  // come from every instant.air_temperature sample in the day (always
  // present, unlike the summary blocks), the displayed weather code is
  // whichever sampled entry falls closest to local midday, and "rain
  // likely" is set if any sample that day forecasts measurable precipitation.
  MetNoDayBucket buckets[cfg::weather::DAILY_FETCH_COUNT];
  int8_t dayIdx = -1;
  for (size_t i = 0; i < series.size(); i++) {
    JsonObjectConst e = series[i].as<JsonObjectConst>();
    const char* t = e["time"];
    int ey = 0, emo = 0, ed = 0, ehh = 0, emi = 0, ess = 0;
    if (t == nullptr || sscanf(t, "%d-%d-%dT%d:%d:%d", &ey, &emo, &ed, &ehh, &emi, &ess) < 5) {
      continue;
    }
    int lly = 0, llmo = 0, lld = 0, llhh = 0, llmi = 0;
    utcToLocal(ey, emo, ed, ehh, emi, offsetHours, lly, llmo, lld, llhh, llmi);

    if (dayIdx < 0 || buckets[dayIdx].y != lly || buckets[dayIdx].m != llmo ||
        buckets[dayIdx].d != lld) {
      if (dayIdx + 1 >= (int)cfg::weather::DAILY_FETCH_COUNT) break;  // enough days collected
      dayIdx++;
      buckets[dayIdx] = MetNoDayBucket{};
      buckets[dayIdx].y = lly;
      buckets[dayIdx].m = llmo;
      buckets[dayIdx].d = lld;
    }
    MetNoDayBucket& b = buckets[dayIdx];

    JsonObjectConst einstant = e["data"]["instant"]["details"].as<JsonObjectConst>();
    const float t2m = einstant["air_temperature"] | 0.0f;
    if (t2m > b.highC) b.highC = t2m;
    if (t2m < b.lowC) b.lowC = t2m;
    const float wind = (einstant["wind_speed"] | 0.0f) * 3.6f;
    if (wind > b.windMaxKmh) b.windMaxKmh = wind;

    JsonObjectConst een1 = e["data"]["next_1_hours"].as<JsonObjectConst>();
    JsonObjectConst een6 = e["data"]["next_6_hours"].as<JsonObjectConst>();
    JsonObjectConst een12 = e["data"]["next_12_hours"].as<JsonObjectConst>();
    const char* esym = nullptr;
    float precip = 0.0f;
    if (!een1.isNull()) {
      esym = een1["summary"]["symbol_code"] | (const char*)nullptr;
      precip = een1["details"]["precipitation_amount"] | 0.0f;
    }
    if (esym == nullptr && !een6.isNull()) {
      esym = een6["summary"]["symbol_code"] | (const char*)nullptr;
      precip = een6["details"]["precipitation_amount"] | 0.0f;
    }
    if (esym == nullptr && !een12.isNull()) {
      esym = een12["summary"]["symbol_code"] | (const char*)nullptr;
    }
    if (precip > 0.1f) b.rainLikely = true;

    const int hourDist = abs(llhh - 12);
    if (esym != nullptr && hourDist < b.bestHourDist) {
      b.bestHourDist = hourDist;
      b.midDaySymbol = esym;
    }
  }
  out.dailyCount = (uint8_t)(dayIdx + 1);
  for (int i = 0; i <= dayIdx; i++) {
    const MetNoDayBucket& b = buckets[i];
    DailyForecastEntry& de = out.daily[i];
    char isoDate[11];
    snprintf(isoDate, sizeof(isoDate), "%04d-%02d-%02d", b.y, b.m, b.d);
    formatDayLabels(isoDate, de.dayLabel, sizeof(de.dayLabel), de.dateLabel, sizeof(de.dateLabel));
    de.weatherCode = wmoCodeFromSymbol(b.midDaySymbol);
    de.highC = b.highC;
    de.lowC = b.lowC;
    de.rainProbabilityPct = b.rainLikely ? 60 : 0;
    de.windSpeedMaxKmh = b.windMaxKmh;
  }

  return true;
}

// Met Office's response is GeoJSON: a FeatureCollection with (for a
// single-point query) exactly one Feature, whose properties.timeSeries is
// the flat array of forecast steps — same shape for both the hourly and
// daily endpoints, just different fields per entry (see
// parseMetOfficeDailyResponse below). Every `time` is UTC with no seconds
// component (e.g. "2023-07-05T10:00Z") — utcToLocal handles that sscanf
// pattern fine since it only demands >=5 parsed fields, and this app only
// ever needs HH:MM anyway.
bool WeatherService::parseMetOfficeHourlyResponse(const String& body, float utcOffsetHoursF,
                                                  WeatherData& out) const {
  const int offsetHours = (int)lroundf(utcOffsetHoursF);

  JsonDocument filter;
  JsonObject tsFilter = filter["features"][0]["properties"]["timeSeries"][0].to<JsonObject>();
  tsFilter["time"] = true;
  tsFilter["screenTemperature"] = true;
  tsFilter["feelsLikeTemperature"] = true;
  tsFilter["screenRelativeHumidity"] = true;
  tsFilter["mslp"] = true;
  tsFilter["windSpeed10m"] = true;
  tsFilter["windDirectionFrom10m"] = true;
  tsFilter["visibility"] = true;
  tsFilter["uvIndex"] = true;
  tsFilter["significantWeatherCode"] = true;
  tsFilter["probOfPrecipitation"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    LOGW(TAG, "Met Office DataHub JSON parse failed: %s", err.c_str());
    return false;
  }

  JsonArrayConst series =
      doc["features"][0]["properties"]["timeSeries"].as<JsonArrayConst>();
  if (series.isNull() || series.size() == 0) {
    LOGW(TAG, "Met Office DataHub JSON missing 'timeSeries'");
    return false;
  }

  // --- current conditions: the first (soonest) entry ---
  JsonObjectConst first = series[0].as<JsonObjectConst>();
  const char* isoUtc = first["time"];
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
  if (isoUtc == nullptr || sscanf(isoUtc, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &hh, &mi, &ss) < 5) {
    LOGW(TAG, "Met Office DataHub JSON missing/malformed timeSeries[0].time");
    return false;
  }
  int ly = 0, lmo = 0, ld = 0, lhh = 0, lmi = 0;
  utcToLocal(y, mo, d, hh, mi, offsetHours, ly, lmo, ld, lhh, lmi);
  snprintf(out.isoTime, sizeof(out.isoTime), "%04d-%02d-%02dT%02d:%02d", ly, lmo, ld, lhh, lmi);
  snprintf(out.lastUpdate, sizeof(out.lastUpdate), "%02d:%02d", lhh, lmi);

  out.temperatureC = first["screenTemperature"] | 0.0f;
  out.feelsLikeC = first["feelsLikeTemperature"] | out.temperatureC;
  out.humidityPct = (int8_t)(first["screenRelativeHumidity"] | 0.0f);
  out.pressureHpa = (first["mslp"] | 0.0f) / 100.0f;  // Pa -> hPa
  out.windSpeedKmh = (first["windSpeed10m"] | 0.0f) * 3.6f;  // m/s -> km/h
  out.windDirectionDeg = (int16_t)(first["windDirectionFrom10m"] | 0.0f);
  out.cloudCoverPct = 0;  // not provided by this product
  out.visibilityKm = (first["visibility"] | 0.0f) / 1000.0f;  // m -> km
  out.uvIndex = first["uvIndex"] | 0.0f;
  const int16_t moCode = first["significantWeatherCode"] | (int16_t)-1;
  out.weatherCode = wmoCodeFromMetOfficeCode(moCode);
  out.isDay = metOfficeCodeIsDay(moCode);

  // --- hourly forecast: best-effort, same reasoning as the other providers
  // — current conditions above already parsed successfully. This product's
  // own forecast horizon (not something this app controls) decides how many
  // entries come back; hourlyCount just reflects whatever that is.
  const size_t n = min((size_t)series.size(), (size_t)cfg::weather::HOURLY_FETCH_COUNT);
  for (size_t i = 0; i < n; i++) {
    JsonObjectConst e = series[i].as<JsonObjectConst>();
    const char* t = e["time"];
    int ey = 0, emo = 0, ed = 0, ehh = 0, emi = 0, ess = 0;
    if (t == nullptr || sscanf(t, "%d-%d-%dT%d:%d:%d", &ey, &emo, &ed, &ehh, &emi, &ess) < 5) {
      continue;
    }
    int lly = 0, llmo = 0, lld = 0, llhh = 0, llmi = 0;
    utcToLocal(ey, emo, ed, ehh, emi, offsetHours, lly, llmo, lld, llhh, llmi);

    HourlyForecastEntry& he = out.hourly[i];
    snprintf(he.time, sizeof(he.time), "%02d:%02d", llhh, llmi);
    he.temperatureC = e["screenTemperature"] | 0.0f;
    const int16_t hourCode = e["significantWeatherCode"] | (int16_t)-1;
    he.weatherCode = wmoCodeFromMetOfficeCode(hourCode);
    he.isDay = metOfficeCodeIsDay(hourCode);
    he.rainProbabilityPct = (int8_t)(e["probOfPrecipitation"] | 0.0f);  // already %
    he.windSpeedKmh = (e["windSpeed10m"] | 0.0f) * 3.6f;
  }
  out.hourlyCount = (uint8_t)n;

  return true;
}

// Daily entries carry separate day*/night* fields rather than one
// high/low — see the sample response this was verified against
// (github.com/openhab/openhab-addons, .../metofficedatahub/.../
// 2022-09-siteDailyResponse.json). The very first entry in the array can be
// missing its day* fields entirely — it's the tail end of the night already
// in progress when the forecast was issued, not really "today" at all
// (confirmed live: showed up as an extra day dated *yesterday* at the front
// of the 5 Day Forecast page). Skipped outright below rather than faked
// into a day via the night-field fallback this used to use.
bool WeatherService::parseMetOfficeDailyResponse(const String& body, float utcOffsetHoursF,
                                                 WeatherData& out) const {
  const int offsetHours = (int)lroundf(utcOffsetHoursF);

  JsonDocument filter;
  JsonObject tsFilter = filter["features"][0]["properties"]["timeSeries"][0].to<JsonObject>();
  tsFilter["time"] = true;
  tsFilter["daySignificantWeatherCode"] = true;
  tsFilter["nightSignificantWeatherCode"] = true;
  tsFilter["dayMaxScreenTemperature"] = true;
  tsFilter["nightMinScreenTemperature"] = true;
  tsFilter["dayProbabilityOfPrecipitation"] = true;
  tsFilter["nightProbabilityOfPrecipitation"] = true;
  tsFilter["midday10MWindSpeed"] = true;
  tsFilter["midnight10MWindSpeed"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    LOGW(TAG, "Met Office DataHub daily JSON parse failed: %s", err.c_str());
    return false;
  }

  JsonArrayConst series =
      doc["features"][0]["properties"]["timeSeries"].as<JsonArrayConst>();
  if (series.isNull() || series.size() == 0) {
    LOGW(TAG, "Met Office DataHub daily JSON missing 'timeSeries'");
    return false;
  }

  // "Today" per the hourly leg of this fetch (parseMetOfficeHourlyResponse,
  // called first — see runFetchMetOfficeDataHub), used below to drop any
  // daily entry dated before it. Confirmed live that the daily endpoint's
  // first entry can be a *fully populated* yesterday (not just a night-only
  // lead-in with day* fields missing, which is all the code here used to
  // check for) — apparently still carried over from a model run that hadn't
  // rolled its daily block forward yet.
  int ty = 0, tmo = 0, td = 0;
  sscanf(out.isoTime, "%d-%d-%d", &ty, &tmo, &td);
  const long todayVal = (long)ty * 10000 + tmo * 100 + td;

  uint8_t outIdx = 0;
  for (size_t i = 0; i < series.size() && outIdx < cfg::weather::DAILY_FETCH_COUNT; i++) {
    JsonObjectConst e = series[i].as<JsonObjectConst>();
    if (e["dayMaxScreenTemperature"].isNull()) continue;  // night-only lead-in entry, not a real day

    const char* t = e["time"];
    int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
    if (t == nullptr || sscanf(t, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &hh, &mi, &ss) < 5) {
      continue;
    }
    int ly = 0, lmo = 0, ld = 0, lhh = 0, lmi = 0;
    utcToLocal(y, mo, d, hh, mi, offsetHours, ly, lmo, ld, lhh, lmi);
    if ((long)ly * 10000 + lmo * 100 + ld < todayVal) continue;  // stale/already-elapsed day
    char isoDate[11];
    snprintf(isoDate, sizeof(isoDate), "%04d-%02d-%02d", ly, lmo, ld);

    DailyForecastEntry& de = out.daily[outIdx];
    formatDayLabels(isoDate, de.dayLabel, sizeof(de.dayLabel), de.dateLabel, sizeof(de.dateLabel));
    const int16_t dayCode = e["daySignificantWeatherCode"] | (int16_t)-1;
    const int16_t nightCode = e["nightSignificantWeatherCode"] | (int16_t)-1;
    de.weatherCode = wmoCodeFromMetOfficeCode(dayCode != -1 ? dayCode : nightCode);
    de.highC = e["dayMaxScreenTemperature"] | 0.0f;
    de.lowC = e["nightMinScreenTemperature"] | (e["dayMaxScreenTemperature"] | 0.0f);
    de.rainProbabilityPct =
        (int8_t)(e["dayProbabilityOfPrecipitation"] | (e["nightProbabilityOfPrecipitation"] | 0.0f));
    const float middayWind = e["midday10MWindSpeed"] | 0.0f;
    const float midnightWind = e["midnight10MWindSpeed"] | 0.0f;
    de.windSpeedMaxKmh = max(middayWind, midnightWind) * 3.6f;
    outIdx++;
  }
  out.dailyCount = outIdx;

  return true;
}

bool WeatherService::parsePirateWeatherResponse(const String& body, float lonForOffsetFallback,
                                                WeatherData& out) const {
  JsonDocument filter;
  filter["offset"] = true;
  JsonObject curFilter = filter["currently"].to<JsonObject>();
  curFilter["time"] = true;
  curFilter["temperature"] = true;
  curFilter["apparentTemperature"] = true;
  curFilter["humidity"] = true;
  curFilter["pressure"] = true;
  curFilter["windSpeed"] = true;
  curFilter["windBearing"] = true;
  curFilter["cloudCover"] = true;
  curFilter["visibility"] = true;
  curFilter["uvIndex"] = true;
  curFilter["icon"] = true;
  // The `[0]` template is ArduinoJson's "apply this shape to every array
  // element" convention — same as Met.no's timeseries[0] filter above.
  JsonObject hourlyItemFilter = filter["hourly"]["data"][0].to<JsonObject>();
  hourlyItemFilter["time"] = true;
  hourlyItemFilter["temperature"] = true;
  hourlyItemFilter["icon"] = true;
  hourlyItemFilter["precipProbability"] = true;
  hourlyItemFilter["windSpeed"] = true;
  JsonObject dailyItemFilter = filter["daily"]["data"][0].to<JsonObject>();
  dailyItemFilter["time"] = true;
  dailyItemFilter["icon"] = true;
  dailyItemFilter["temperatureHigh"] = true;
  dailyItemFilter["temperatureLow"] = true;
  dailyItemFilter["precipProbability"] = true;
  dailyItemFilter["windSpeed"] = true;
  dailyItemFilter["sunriseTime"] = true;
  dailyItemFilter["sunsetTime"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    LOGW(TAG, "Pirate Weather JSON parse failed: %s", err.c_str());
    return false;
  }

  JsonObjectConst current = doc["currently"].as<JsonObjectConst>();
  const long currentTimeUtc = current["time"] | 0L;
  if (current.isNull() || currentTimeUtc == 0) {
    LOGW(TAG, "Pirate Weather JSON missing 'currently'");
    return false;
  }
  // `offset` is Pirate Weather's fixed (non-DST-aware) UTC offset in hours
  // for this location — more accurate than Met.no's round(longitude/15)
  // estimate when present, so it's preferred; that estimate is still the
  // fallback if the field is ever missing. Rounded to a whole hour either
  // way, same whole-hour-only limitation as epochToLocalParts() above.
  const float offsetRaw = doc["offset"] | (lonForOffsetFallback / 15.0f);
  const int offsetHours = (int)lroundf(offsetRaw);

  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  epochToLocalParts(currentTimeUtc, offsetHours, y, mo, d, hh, mi);
  snprintf(out.isoTime, sizeof(out.isoTime), "%04d-%02d-%02dT%02d:%02d", y, mo, d, hh, mi);
  snprintf(out.lastUpdate, sizeof(out.lastUpdate), "%02d:%02d", hh, mi);

  out.temperatureC = current["temperature"] | 0.0f;
  out.feelsLikeC = current["apparentTemperature"] | out.temperatureC;
  out.humidityPct = (int8_t)((current["humidity"] | 0.0f) * 100.0f);
  out.pressureHpa = current["pressure"] | 0.0f;
  out.windSpeedKmh = (current["windSpeed"] | 0.0f) * 3.6f;  // si units = m/s
  out.windDirectionDeg = (int16_t)(current["windBearing"] | 0.0f);
  out.cloudCoverPct = (int8_t)((current["cloudCover"] | 0.0f) * 100.0f);
  out.visibilityKm = current["visibility"] | 0.0f;  // si units = km already
  out.uvIndex = current["uvIndex"] | 0.0f;
  const char* icon = current["icon"] | (const char*)nullptr;
  out.weatherCode = wmoCodeFromPirateIcon(icon);
  out.isDay = pirateIconIsDay(icon);

  // Best-effort, same reasoning as Open-Meteo/Met.no's hourly blocks above —
  // current conditions already parsed successfully at this point.
  JsonArrayConst hourlyData = doc["hourly"]["data"].as<JsonArrayConst>();
  if (!hourlyData.isNull()) {
    const size_t n = min((size_t)hourlyData.size(), (size_t)cfg::weather::HOURLY_FETCH_COUNT);
    for (size_t i = 0; i < n; i++) {
      JsonObjectConst e = hourlyData[i].as<JsonObjectConst>();
      const long t = e["time"] | 0L;
      int ey = 0, emo = 0, ed = 0, ehh = 0, emi = 0;
      epochToLocalParts(t, offsetHours, ey, emo, ed, ehh, emi);

      HourlyForecastEntry& he = out.hourly[i];
      snprintf(he.time, sizeof(he.time), "%02d:%02d", ehh, emi);
      he.temperatureC = e["temperature"] | 0.0f;
      const char* hourIcon = e["icon"] | (const char*)nullptr;
      he.weatherCode = wmoCodeFromPirateIcon(hourIcon);
      he.isDay = pirateIconIsDay(hourIcon);
      he.rainProbabilityPct = (int8_t)((e["precipProbability"] | 0.0f) * 100.0f);
      he.windSpeedKmh = (e["windSpeed"] | 0.0f) * 3.6f;
    }
    out.hourlyCount = (uint8_t)n;
  }

  // Best-effort, same reasoning.
  JsonArrayConst dailyData = doc["daily"]["data"].as<JsonArrayConst>();
  if (!dailyData.isNull()) {
    const size_t n = min((size_t)dailyData.size(), (size_t)cfg::weather::DAILY_FETCH_COUNT);
    for (size_t i = 0; i < n; i++) {
      JsonObjectConst e = dailyData[i].as<JsonObjectConst>();
      const long t = e["time"] | 0L;
      int ey = 0, emo = 0, ed = 0, ehh = 0, emi = 0;
      epochToLocalParts(t, offsetHours, ey, emo, ed, ehh, emi);
      char isoDate[11];
      snprintf(isoDate, sizeof(isoDate), "%04d-%02d-%02d", ey, emo, ed);

      DailyForecastEntry& de = out.daily[i];
      formatDayLabels(isoDate, de.dayLabel, sizeof(de.dayLabel), de.dateLabel, sizeof(de.dateLabel));
      const char* dayIcon = e["icon"] | (const char*)nullptr;
      de.weatherCode = wmoCodeFromPirateIcon(dayIcon);
      de.highC = e["temperatureHigh"] | 0.0f;
      de.lowC = e["temperatureLow"] | 0.0f;
      de.rainProbabilityPct = (int8_t)((e["precipProbability"] | 0.0f) * 100.0f);
      de.windSpeedMaxKmh = (e["windSpeed"] | 0.0f) * 3.6f;
    }
    out.dailyCount = (uint8_t)n;

    // Sun & Moon page only ever needs *today's* sunrise/sunset (index 0) —
    // same convention as lastUpdate above.
    if (n > 0) {
      JsonObjectConst today = dailyData[0].as<JsonObjectConst>();
      const long sr = today["sunriseTime"] | 0L;
      const long ss = today["sunsetTime"] | 0L;
      if (sr > 0) {
        int sy = 0, smo = 0, sd = 0, shh = 0, smi = 0;
        epochToLocalParts(sr, offsetHours, sy, smo, sd, shh, smi);
        snprintf(out.sunriseTime, sizeof(out.sunriseTime), "%02d:%02d", shh, smi);
      } else {
        strlcpy(out.sunriseTime, "--:--", sizeof(out.sunriseTime));
      }
      if (ss > 0) {
        int sy = 0, smo = 0, sd = 0, shh = 0, smi = 0;
        epochToLocalParts(ss, offsetHours, sy, smo, sd, shh, smi);
        snprintf(out.sunsetTime, sizeof(out.sunsetTime), "%02d:%02d", shh, smi);
      } else {
        strlcpy(out.sunsetTime, "--:--", sizeof(out.sunsetTime));
      }
    }
  }

  return true;
}

WeatherData WeatherService::data() const {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  WeatherData copy = _data;
  xSemaphoreGive(_mutex);
  return copy;
}
