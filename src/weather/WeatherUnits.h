#pragma once

#include <Arduino.h>

// Units for values the weather feature produces, plus the conversions from
// Open-Meteo's native metric units (the only units WeatherData ever stores —
// see weather/WeatherData.h) to whichever unit AppSettings::tempUnit/
// windUnit/pressureUnit selects. Settings store the enum as a raw uint8_t
// (see cfg::units in config/AppConfig.h for the matching defaults/bounds) —
// the same convention already used for LogLevel.
//
// Every live page converts at render time via these functions rather than
// switching on the enum itself — one place to fix/extend if a unit's
// conversion factor or suffix ever needs to change.
enum class TempUnit : uint8_t { Celsius = 0, Fahrenheit = 1 };
enum class WindUnit : uint8_t { KilometersPerHour = 0, MilesPerHour = 1, MetersPerSecond = 2, Knots = 3 };
enum class PressureUnit : uint8_t { HectoPascal = 0, InchesOfMercury = 1, MillimetersOfMercury = 2 };

// Which weather data source weather/WeatherService.cpp fetches from — see
// that file for each provider's request/parse implementation. All
// normalize into the exact same WeatherData shape (WMO-style weather codes,
// km/h wind, local "HH:MM" times), so nothing outside WeatherService.cpp
// needs to know which provider is active.
enum class WeatherProvider : uint8_t {
  OpenMeteoAuto = 0,       // Open-Meteo, best-match model blend (the original default)
  OpenMeteoUkmo = 1,       // Open-Meteo, pinned to the UK Met Office UKV model
  MetNo = 2,               // MET Norway (api.met.no) — no API key, HTTPS only
  MetOfficeDataHub = 3,    // Met Office Weather DataHub "Global Spot" — needs an API key
  PirateWeather = 4,       // Pirate Weather (pirateweather.net) — Dark Sky-compatible, needs an API key
  // 5.. — the rest of Open-Meteo's national-model catalogue (same free,
  // keyless /v1/forecast endpoint and response schema as OpenMeteoUkmo, just
  // a different `models=` slug — see openMeteoModelSlug() below). Values and
  // slugs verified against openapi/forecast.yml in the open-meteo/open-meteo
  // GitHub repo, not the docs site (which lists a "CHMI Aladin" model that
  // doesn't actually exist in the live models= enum).
  OpenMeteoDwd = 5,          // Germany — DWD ICON
  OpenMeteoGfs = 6,          // USA — NOAA GFS
  OpenMeteoMeteoFrance = 7,  // France — Meteo-France
  OpenMeteoEcmwf = 8,        // ECMWF (no seamless variant; pins to its flagship IFS 0.25 deg)
  OpenMeteoKma = 9,          // South Korea — KMA
  OpenMeteoJma = 10,         // Japan — JMA
  OpenMeteoMeteoSwiss = 11,  // Switzerland — MeteoSwiss ICON
  OpenMeteoMetNoModel = 12,  // Norway — MET Norway model via Open-Meteo (distinct from the
                             // direct api.met.no integration at WeatherProvider::MetNo)
  OpenMeteoGem = 13,         // Canada — GEM
  OpenMeteoBom = 14,         // Australia — BOM (no seamless variant)
  OpenMeteoCma = 15,         // China — CMA (no seamless variant)
  OpenMeteoKnmi = 16,        // Netherlands — KNMI
  OpenMeteoDmi = 17,         // Denmark — DMI
  OpenMeteoItaliaMeteo = 18, // Italy — ItaliaMeteo (no seamless variant)
  OpenMeteoGeosphere = 19,   // Austria — GeoSphere
};

inline const char* weatherProviderName(WeatherProvider provider) {
  switch (provider) {
    case WeatherProvider::OpenMeteoUkmo:
      return "Open-Meteo (UK Met Office)";
    case WeatherProvider::MetNo:
      return "MET Norway (direct)";
    case WeatherProvider::MetOfficeDataHub:
      return "Met Office DataHub (UK)";
    case WeatherProvider::PirateWeather:
      return "Pirate Weather";
    case WeatherProvider::OpenMeteoDwd:
      return "Open-Meteo (Germany/DWD ICON)";
    case WeatherProvider::OpenMeteoGfs:
      return "Open-Meteo (USA/NOAA GFS)";
    case WeatherProvider::OpenMeteoMeteoFrance:
      return "Open-Meteo (France/Meteo-France)";
    case WeatherProvider::OpenMeteoEcmwf:
      return "Open-Meteo (ECMWF)";
    case WeatherProvider::OpenMeteoKma:
      return "Open-Meteo (South Korea/KMA)";
    case WeatherProvider::OpenMeteoJma:
      return "Open-Meteo (Japan/JMA)";
    case WeatherProvider::OpenMeteoMeteoSwiss:
      return "Open-Meteo (Switzerland/MeteoSwiss)";
    case WeatherProvider::OpenMeteoMetNoModel:
      return "Open-Meteo (Norway/MET)";
    case WeatherProvider::OpenMeteoGem:
      return "Open-Meteo (Canada/GEM)";
    case WeatherProvider::OpenMeteoBom:
      return "Open-Meteo (Australia/BOM)";
    case WeatherProvider::OpenMeteoCma:
      return "Open-Meteo (China/CMA)";
    case WeatherProvider::OpenMeteoKnmi:
      return "Open-Meteo (Netherlands/KNMI)";
    case WeatherProvider::OpenMeteoDmi:
      return "Open-Meteo (Denmark/DMI)";
    case WeatherProvider::OpenMeteoItaliaMeteo:
      return "Open-Meteo (Italy/ItaliaMeteo)";
    case WeatherProvider::OpenMeteoGeosphere:
      return "Open-Meteo (Austria/GeoSphere)";
    case WeatherProvider::OpenMeteoAuto:
    default:
      return "Open-Meteo (Auto)";
  }
}

// Which `models=` slug (if any) runFetchOpenMeteo() should pin its request
// to for a given provider. nullptr means no pin needed: either
// OpenMeteoAuto (best-match is the default with no `models=` param at all),
// or a provider with its own runFetch* implementation that never reaches
// this lookup.
inline const char* openMeteoModelSlug(WeatherProvider provider) {
  switch (provider) {
    case WeatherProvider::OpenMeteoUkmo:
      return "ukmo_seamless";
    case WeatherProvider::OpenMeteoDwd:
      return "icon_seamless";
    case WeatherProvider::OpenMeteoGfs:
      return "ncep_gfs_seamless";
    case WeatherProvider::OpenMeteoMeteoFrance:
      return "meteofrance_seamless";
    case WeatherProvider::OpenMeteoEcmwf:
      return "ecmwf_ifs025";
    case WeatherProvider::OpenMeteoKma:
      return "kma_seamless";
    case WeatherProvider::OpenMeteoJma:
      return "jma_seamless";
    case WeatherProvider::OpenMeteoMeteoSwiss:
      return "meteoswiss_icon_seamless";
    case WeatherProvider::OpenMeteoMetNoModel:
      return "metno_seamless";
    case WeatherProvider::OpenMeteoGem:
      return "cmc_gem_seamless";
    case WeatherProvider::OpenMeteoBom:
      return "bom_access_global";
    case WeatherProvider::OpenMeteoCma:
      return "cma_grapes_global";
    case WeatherProvider::OpenMeteoKnmi:
      return "knmi_seamless";
    case WeatherProvider::OpenMeteoDmi:
      return "dmi_seamless";
    case WeatherProvider::OpenMeteoItaliaMeteo:
      return "italia_meteo_arpae_icon_2i";
    case WeatherProvider::OpenMeteoGeosphere:
      return "geosphere_seamless";
    default:
      return nullptr;
  }
}

inline float convertTempC(float celsius, TempUnit unit) {
  return unit == TempUnit::Fahrenheit ? celsius * 9.0f / 5.0f + 32.0f : celsius;
}

inline const char* tempUnitSuffix(TempUnit unit) {
  return unit == TempUnit::Fahrenheit ? "F" : "C";
}

inline float convertWindKmh(float kmh, WindUnit unit) {
  switch (unit) {
    case WindUnit::MilesPerHour:
      return kmh * 0.621371f;
    case WindUnit::MetersPerSecond:
      return kmh / 3.6f;
    case WindUnit::Knots:
      return kmh * 0.539957f;
    default:
      return kmh;
  }
}

inline const char* windUnitSuffix(WindUnit unit) {
  switch (unit) {
    case WindUnit::MilesPerHour:
      return "mph";
    case WindUnit::MetersPerSecond:
      return "m/s";
    case WindUnit::Knots:
      return "kn";
    default:
      return "km/h";
  }
}

inline float convertPressureHpa(float hpa, PressureUnit unit) {
  switch (unit) {
    case PressureUnit::InchesOfMercury:
      return hpa * 0.0295300f;
    case PressureUnit::MillimetersOfMercury:
      return hpa * 0.750062f;
    default:
      return hpa;
  }
}

inline const char* pressureUnitSuffix(PressureUnit unit) {
  switch (unit) {
    case PressureUnit::InchesOfMercury:
      return "inHg";
    case PressureUnit::MillimetersOfMercury:
      return "mmHg";
    default:
      return "hPa";
  }
}
