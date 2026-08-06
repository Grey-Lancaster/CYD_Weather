#include "weather/WeatherAlertManager.h"

#include <stdarg.h>
#include <stdio.h>

#include "storage/SettingsManager.h"
#include "storage/WeatherHistoryManager.h"
#include "utils/Log.h"

static const char* TAG = "ALERT";

namespace {

// Fixed severity per alert type — see WeatherAlertManager.h's comment on
// alertSeverity().
constexpr AlertSeverity kSeverityByType[(size_t)AlertType::Count] = {
    AlertSeverity::Advisory,  // HighWind
    AlertSeverity::Warning,   // VeryHighWind
    AlertSeverity::Warning,   // HeavyRain
    AlertSeverity::Danger,    // Thunderstorm
    AlertSeverity::Advisory,  // Snow
    AlertSeverity::Danger,    // IceRisk
    AlertSeverity::Information,  // Fog
    AlertSeverity::Warning,   // HeatWarning
    AlertSeverity::Warning,   // ColdWarning
    AlertSeverity::Advisory,  // PressureDropRapid
    AlertSeverity::Information,  // PressureRiseRapid
    AlertSeverity::Advisory,  // HighUV
    AlertSeverity::Advisory,  // PoorAirQuality (never produced — see header)
};

// Kept short (<=20 chars) on purpose — these are drawn both in the thin
// status-bar banner and in the alert-list cards (see ui/StatusBar.cpp,
// pages/PageAlerts.cpp), neither of which has room for a long headline.
const char* kHeadlineByType[(size_t)AlertType::Count] = {
    "Strong Winds",       // HighWind
    "Dangerous Winds",    // VeryHighWind
    "Heavy Rain Expected",// HeavyRain
    "Thunderstorm Risk",  // Thunderstorm
    "Snow Expected",      // Snow
    "Ice Risk",           // IceRisk
    "Foggy Conditions",   // Fog
    "Heat Warning",       // HeatWarning
    "Cold Warning",       // ColdWarning
    "Pressure Dropping",  // PressureDropRapid
    "Pressure Rising",    // PressureRiseRapid
    "High UV Today",      // HighUV
    "Poor Air Quality",   // PoorAirQuality (never produced — see header)
};

// ---- Open-Meteo WMO weather_code classifiers (weather/WeatherData.h) —
// deliberately separate from weather/WeatherIcons.h's category mapping,
// since alert triage needs finer buckets than the icon set does (e.g.
// "heavy rain" specifically, not just "rain").
bool isThunderstormCode(int16_t c) { return c == 95 || c == 96 || c == 99; }
bool isHeavyRainCode(int16_t c) { return c == 65 || c == 67 || c == 82; }
bool isAnyRainCode(int16_t c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82); }
bool isSnowCode(int16_t c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }
bool isFogCode(int16_t c) { return c == 45 || c == 48; }
bool isFreezingCode(int16_t c) { return c == 56 || c == 57 || c == 66 || c == 67; }

// A margin below which "cold" also means "wet surfaces can ice over" — not
// a separate user-configurable threshold (the brief's settings examples
// list Wind/Rain/Heat/Cold/Pressure only), just a fixed offset applied on
// top of whatever ColdWarning is configured to.
constexpr float kIcePointC = 2.0f;

// Scans the hourly forecast forward from "now" (index 0 is always the
// soonest hour — see WeatherData::hourly) for how many consecutive hours
// `pred` keeps holding. Used as a rough "expected duration" for the alerts
// driven off the hourly block — real forecast data, not a guess.
template <typename Pred>
uint16_t consecutiveHoursMin(const WeatherData& d, Pred pred) {
  uint8_t hours = 0;
  for (uint8_t i = 0; i < d.hourlyCount; i++) {
    if (!pred(d.hourly[i])) break;
    hours++;
  }
  return (uint16_t)hours * 60;
}

// At least this many logged readings (~2h at the 15-minute logging floor —
// see cfg::history::MIN_INTERVAL_S) must exist before a pressure-trend
// alert is evaluated at all — with less history than that, a short/noisy
// window could look like a "rapid" change that isn't.
constexpr uint8_t kPressureLookback = 12;     // ~3h — the trend window itself
constexpr uint8_t kPressureLookbackMin = 8;   // ~2h — minimum history required

// `outDeltaHpa` = current - reading from ~3h ago (positive = rising).
// Returns false (leaving outDeltaHpa untouched) if there isn't enough
// logged history yet to trust the comparison.
bool pressureTrendHpa(float currentHpa, float& outDeltaHpa) {
  WeatherHistoryReading readings[kPressureLookback];
  const size_t n = WeatherHistoryManager::instance().getHistory(readings, kPressureLookback);
  if (n < kPressureLookbackMin) return false;
  outDeltaHpa = currentHpa - readings[0].pressureHpa;  // oldest of the slice = ~3h ago
  return true;
}

}  // namespace

AlertSeverity alertSeverity(AlertType type) {
  const uint8_t i = (uint8_t)type;
  return i < (uint8_t)AlertType::Count ? kSeverityByType[i] : AlertSeverity::Information;
}

namespace {

// Staging area evaluate() builds candidates into before they're compared
// against the previous active set (to decide whether anything actually
// changed) and copied into WeatherAlertManager::_active.
struct Candidate {
  AlertType type;
  char reason[64];
  uint16_t durationMin;
};

void pushCandidate(Candidate* out, uint8_t& count, AlertType type, uint16_t durationMin,
                   const char* reasonFmt, ...) __attribute__((format(printf, 5, 6)));

void pushCandidate(Candidate* out, uint8_t& count, AlertType type, uint16_t durationMin,
                   const char* reasonFmt, ...) {
  if (count >= WeatherAlertManager::MAX_ALERTS) {
    LOGW(TAG, "alert list full, dropping type %u", (unsigned)type);
    return;
  }
  Candidate& c = out[count++];
  c.type = type;
  c.durationMin = durationMin;
  va_list args;
  va_start(args, reasonFmt);
  vsnprintf(c.reason, sizeof(c.reason), reasonFmt, args);
  va_end(args);
}

}  // namespace

void WeatherAlertManager::evaluate(const WeatherData& data) {
  const AppSettings& settings = SettingsManager::instance().get();

  if (!settings.alertsEnabled || !data.valid) {
    if (_activeCount != 0) _version++;
    _activeCount = 0;
    return;
  }

  Candidate candidates[MAX_ALERTS];
  uint8_t count = 0;

  // ---- Wind (High / Very High — mutually exclusive, higher tier wins).
  {
    float maxWind = data.windSpeedKmh;
    const uint8_t hours = min(data.hourlyCount, (uint8_t)12);
    for (uint8_t i = 0; i < hours; i++) maxWind = max(maxWind, data.hourly[i].windSpeedKmh);

    if (maxWind >= settings.alertWindDangerKmh) {
      const uint16_t dur = consecutiveHoursMin(
          data, [&](const HourlyForecastEntry& h) { return h.windSpeedKmh >= settings.alertWindDangerKmh; });
      pushCandidate(candidates, count, AlertType::VeryHighWind, dur,
                    "Wind speeds forecast up to %.0f km/h", (double)maxWind);
    } else if (maxWind >= settings.alertWindWarningKmh) {
      const uint16_t dur = consecutiveHoursMin(
          data, [&](const HourlyForecastEntry& h) { return h.windSpeedKmh >= settings.alertWindWarningKmh; });
      pushCandidate(candidates, count, AlertType::HighWind, dur,
                    "Wind speeds forecast up to %.0f km/h", (double)maxWind);
    }
  }

  // ---- Heavy rain / thunderstorm / snow / fog: scan the next 12h of
  // hourly codes for the first matching hour, so the reason/duration are
  // anchored to real forecast data rather than "current conditions only".
  {
    const uint8_t hours = min(data.hourlyCount, (uint8_t)12);

    for (uint8_t i = 0; i < hours; i++) {
      if (isHeavyRainCode(data.hourly[i].weatherCode) &&
          data.hourly[i].rainProbabilityPct >= settings.alertRainProbWarningPct) {
        const uint16_t dur = consecutiveHoursMin(
            data, [&](const HourlyForecastEntry& h) { return isAnyRainCode(h.weatherCode); });
        pushCandidate(candidates, count, AlertType::HeavyRain, dur,
                      "%d%% chance of heavy rain around %s", (int)data.hourly[i].rainProbabilityPct,
                      data.hourly[i].time);
        break;
      }
    }

    for (uint8_t i = 0; i < hours; i++) {
      if (isThunderstormCode(data.hourly[i].weatherCode)) {
        const uint16_t dur = consecutiveHoursMin(
            data, [](const HourlyForecastEntry& h) { return isThunderstormCode(h.weatherCode); });
        pushCandidate(candidates, count, AlertType::Thunderstorm, dur, "Thunderstorms forecast around %s",
                      data.hourly[i].time);
        break;
      }
    }

    for (uint8_t i = 0; i < hours; i++) {
      if (isSnowCode(data.hourly[i].weatherCode)) {
        const uint16_t dur = consecutiveHoursMin(
            data, [](const HourlyForecastEntry& h) { return isSnowCode(h.weatherCode); });
        pushCandidate(candidates, count, AlertType::Snow, dur, "Snow forecast around %s",
                      data.hourly[i].time);
        break;
      }
    }

    if (isFogCode(data.weatherCode)) {
      const uint16_t dur =
          consecutiveHoursMin(data, [](const HourlyForecastEntry& h) { return isFogCode(h.weatherCode); });
      pushCandidate(candidates, count, AlertType::Fog, dur, "Reduced visibility from fog");
    }

    // Ice risk: cold enough for standing water to freeze, and either an
    // explicitly-freezing code or a real chance of precipitation.
    for (uint8_t i = 0; i < hours; i++) {
      const HourlyForecastEntry& h = data.hourly[i];
      const bool cold = h.temperatureC <= settings.alertColdWarningC + kIcePointC;
      const bool wet = isFreezingCode(h.weatherCode) || h.rainProbabilityPct >= 40;
      if (cold && wet) {
        const uint16_t dur = consecutiveHoursMin(data, [&](const HourlyForecastEntry& hh) {
          return hh.temperatureC <= settings.alertColdWarningC + kIcePointC &&
                (isFreezingCode(hh.weatherCode) || hh.rainProbabilityPct >= 40);
        });
        pushCandidate(candidates, count, AlertType::IceRisk, dur,
                      "Temperatures near %.0fC with wet conditions", (double)h.temperatureC);
        break;
      }
    }
  }

  // ---- Heat / cold: current feels-like, or today's forecast high/low.
  {
    const float feelsLike = data.feelsLikeC;
    const float todayHigh = data.dailyCount > 0 ? data.daily[0].highC : feelsLike;
    const float todayLow = data.dailyCount > 0 ? data.daily[0].lowC : feelsLike;

    if (feelsLike >= settings.alertHeatWarningC || todayHigh >= settings.alertHeatWarningC) {
      pushCandidate(candidates, count, AlertType::HeatWarning, 0, "Feels like %.0fC, high of %.0fC expected",
                    (double)feelsLike, (double)todayHigh);
    }
    if (feelsLike <= settings.alertColdWarningC || todayLow <= settings.alertColdWarningC) {
      pushCandidate(candidates, count, AlertType::ColdWarning, 0, "Feels like %.0fC, low of %.0fC expected",
                    (double)feelsLike, (double)todayLow);
    }
  }

  // ---- Rapid pressure change — needs enough logged history (see
  // pressureTrendHpa()); silently skipped otherwise (not a "no alert",
  // just "not enough data yet", same spirit as this project's other
  // best-effort history-backed features).
  {
    float deltaHpa = 0.0f;
    if (pressureTrendHpa(data.pressureHpa, deltaHpa)) {
      if (deltaHpa <= -settings.alertPressureChangeHpa) {
        pushCandidate(candidates, count, AlertType::PressureDropRapid, 0,
                      "Pressure fell %.1f hPa in the last ~3h", (double)(-deltaHpa));
      } else if (deltaHpa >= settings.alertPressureChangeHpa) {
        pushCandidate(candidates, count, AlertType::PressureRiseRapid, 0,
                      "Pressure rose %.1f hPa in the last ~3h", (double)deltaHpa);
      }
    }
  }

  // ---- UV.
  if (data.uvIndex >= settings.alertUvWarningIndex) {
    pushCandidate(candidates, count, AlertType::HighUV, 0, "UV index %.0f today", (double)data.uvIndex);
  }

  // ---- Resolve issued time: carry over from the previous active set if
  // this type was already active, otherwise stamp it "now" (best-effort —
  // see class comment in the header).
  WeatherAlert fresh[MAX_ALERTS];
  for (uint8_t i = 0; i < count; i++) {
    WeatherAlert& a = fresh[i];
    a.type = candidates[i].type;
    a.severity = alertSeverity(a.type);
    strlcpy(a.message, kHeadlineByType[(uint8_t)a.type], sizeof(a.message));
    strlcpy(a.reason, candidates[i].reason, sizeof(a.reason));
    a.expectedDurationMin = candidates[i].durationMin;

    const char* previousIssued = nullptr;
    for (uint8_t j = 0; j < _activeCount; j++) {
      if (_active[j].type == a.type) {
        previousIssued = _active[j].issued;
        break;
      }
    }
    strlcpy(a.issued, previousIssued != nullptr ? previousIssued : data.lastUpdate, sizeof(a.issued));
  }

  // Most-severe-first (stable insertion sort — at most MAX_ALERTS elements).
  for (uint8_t i = 1; i < count; i++) {
    WeatherAlert key = fresh[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && (uint8_t)fresh[j].severity < (uint8_t)key.severity) {
      fresh[j + 1] = fresh[j];
      j--;
    }
    fresh[j + 1] = key;
  }

  // Change detection — a byte-for-byte compare of the (now identically
  // ordered) active sets is simplest and cheap at this size (<=8 entries).
  bool changed = (count != _activeCount);
  if (!changed) {
    for (uint8_t i = 0; i < count; i++) {
      const WeatherAlert& a = fresh[i];
      const WeatherAlert& b = _active[i];
      if (a.type != b.type || a.severity != b.severity || a.expectedDurationMin != b.expectedDurationMin ||
          strcmp(a.reason, b.reason) != 0 || strcmp(a.issued, b.issued) != 0) {
        changed = true;
        break;
      }
    }
  }

  for (uint8_t i = 0; i < count; i++) _active[i] = fresh[i];
  _activeCount = count;
  if (changed) _version++;
}
