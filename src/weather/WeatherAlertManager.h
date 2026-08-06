#pragma once

#include <Arduino.h>

#include "weather/WeatherData.h"

// Local, on-device weather alert engine — no external alert service. Reads
// a WeatherData snapshot (current + hourly + 5-day, all from the one shared
// Open-Meteo fetch — see weather/WeatherService.h) plus
// storage/WeatherHistoryManager's already-logged readings (for the
// pressure-trend alerts only) and turns them into a small, human-readable
// active-alert list, checked against configurable thresholds
// (storage/SettingsManager.h's schema-5 `alert*` fields).
//
// Deliberately has no dependency on TFT_eSPI or any drawing code — see
// ui/AlertTheme.h for how a severity is coloured/iconified, and
// ui/StatusBar.cpp / pages/PageAlerts.cpp for where alerts actually get
// drawn on screen.
//
// evaluate() does real work (scans the hourly array, reads back recent
// history for the pressure trend) and is meant to be called only when a new
// weather fetch lands — see main.cpp's "alerts.eval" scheduler task, which
// watches WeatherService::dataVersion() the same way "history.log" already
// does — not on every tick, per the brief's "evaluate only when new data
// arrives, do not continuously calculate".

enum class AlertType : uint8_t {
  HighWind,
  VeryHighWind,
  HeavyRain,
  Thunderstorm,
  Snow,
  IceRisk,
  Fog,
  HeatWarning,
  ColdWarning,
  PressureDropRapid,
  PressureRiseRapid,
  HighUV,
  // Future support: Open-Meteo air-quality data isn't fetched (a second API
  // endpoint/request this project doesn't make — "no external alert
  // service" and "don't duplicate downloads"), so this type is fully
  // modelled (colour/icon/priority all resolve normally) but evaluate()
  // never actually produces one yet.
  PoorAirQuality,
  Count
};

enum class AlertSeverity : uint8_t { Information = 0, Advisory = 1, Warning = 2, Danger = 3 };

// Fixed per AlertType (see WeatherAlertManager.cpp's kSeverityByType) rather
// than derived from how far a reading is past its threshold — the brief
// already splits wind into two types by severity ("High Wind" vs "Very High
// Wind"), so alert *type* alone determines severity for every alert here.
AlertSeverity alertSeverity(AlertType type);

struct WeatherAlert {
  AlertType type = AlertType::HighWind;
  AlertSeverity severity = AlertSeverity::Information;
  char message[48] = {0};  // headline, e.g. "Strong Winds Expected" — banner/list text
  char reason[64] = {0};   // e.g. "Sustained wind forecast to reach 52 km/h"
  char issued[6] = {0};    // "HH:MM" — best-effort, see class comment above
  uint16_t expectedDurationMin = 0;  // 0 = unknown/ongoing (e.g. pressure-trend alerts)
};

class WeatherAlertManager {
 public:
  static constexpr uint8_t MAX_ALERTS = 8;

  // Re-evaluates every supported alert type against `data` and replaces the
  // active-alert list (sorted most-severe-first). An alert that was already
  // active keeps its original `issued` stamp rather than being re-stamped
  // "now" on every call; a newly-appearing one is stamped from
  // `data.lastUpdate`. Does nothing if AppSettings::alertsEnabled is off
  // (clears any existing alerts instead).
  void evaluate(const WeatherData& data);

  uint8_t activeCount() const { return _activeCount; }
  const WeatherAlert& activeAt(uint8_t index) const { return _active[index]; }
  bool hasActive() const { return _activeCount > 0; }
  // Highest-severity active alert (ties broken by AlertType order), or
  // nullptr if none — what the status-bar banner shows.
  const WeatherAlert* topAlert() const { return _activeCount > 0 ? &_active[0] : nullptr; }

  // Bumped whenever evaluate() changes the resulting active set (an alert
  // added, removed, or its reason/duration changed) — a cheap dirty-check
  // for callers that should only redraw when something actually changed
  // (ui/StatusBar.cpp, pages/PageAlerts.cpp), same pattern as
  // WeatherService::dataVersion().
  uint32_t version() const { return _version; }

 private:
  WeatherAlert _active[MAX_ALERTS];
  uint8_t _activeCount = 0;
  uint32_t _version = 0;
};
