#pragma once

#include <Arduino.h>

// The reusable weather condition/icon engine: every page goes through this
// module to turn a raw Open-Meteo `weather_code` into something displayable.
// No page should ever switch on weather_code itself — that logic lives here,
// once.
//
// Deliberately has no dependency on ui/ or TFT_eSPI: this is the weather
// domain's vocabulary (which category a code belongs to), not how it's
// drawn. The actual icon art lives in ui/WeatherIconTheme.h, which depends
// on WeatherIconId (not the other way round) — swapping icon themes later
// never touches this file.

// Distinct icon categories the icon theme can draw. Only Clear and Partly
// Cloudy have separate day/night art — every other condition looks the same
// at any hour (see getWeatherIcon()).
enum class WeatherIconId : uint8_t {
  ClearDay,
  ClearNight,
  PartlyCloudyDay,
  PartlyCloudyNight,
  Cloudy,
  Overcast,
  Fog,
  Drizzle,
  Rain,
  HeavyRain,
  Showers,
  Thunderstorm,
  Snow,
  HeavySnow,
  Unknown
};

// Long, precise description, e.g. "Depositing Rime Fog". Falls back to
// "Unknown" for a code not in the table.
const char* getWeatherDescription(int16_t code);

// Short, space-constrained category label, e.g. "Fog" — for tight UI like
// the Hourly Forecast table. Same fallback as getWeatherDescription().
const char* getShortDescription(int16_t code);

// Which icon to draw for `code`. `isDay` (Open-Meteo's is_day field) only
// changes the result for Clear/Partly Cloudy — everything else ignores it.
WeatherIconId getWeatherIcon(int16_t code, bool isDay);

// Fixed, theme-independent colour for the condition's broad category (rain
// is blue whether the UI is in dark or light mode) — an RGB565 value ready
// to hand straight to a TFT_eSPI draw call.
uint16_t getWeatherColour(int16_t code);
