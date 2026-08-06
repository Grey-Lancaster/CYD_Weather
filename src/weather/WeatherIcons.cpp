#include "weather/WeatherIcons.h"

namespace {

// Local RGB565 packer — deliberately not shared with ui/Theme.h's rgb565()
// so this module stays free of any ui/TFT_eSPI dependency (see the header).
constexpr uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Fixed per-category colours (see WEATHER CODE LOOKUP in weather/README.md
// for the reasoning) — intentionally not sourced from the active Theme;
// "rain is blue" regardless of dark/light mode.
constexpr uint16_t kColourClear = packRgb565(90, 170, 250);     // sky blue
constexpr uint16_t kColourCloudy = packRgb565(140, 150, 160);   // grey
constexpr uint16_t kColourRain = packRgb565(60, 120, 210);      // deeper blue
constexpr uint16_t kColourStorm = packRgb565(150, 90, 200);     // purple
constexpr uint16_t kColourSnow = packRgb565(240, 245, 250);     // near-white
constexpr uint16_t kColourFog = packRgb565(190, 196, 205);      // light grey
constexpr uint16_t kColourUnknown = packRgb565(120, 120, 120);  // neutral grey

struct WeatherCodeEntry {
  int16_t code;
  const char* shortDesc;
  const char* longDesc;
  WeatherIconId dayIcon;
  WeatherIconId nightIcon;  // == dayIcon for conditions with no night variant
  uint16_t colour;
};

using Icon = WeatherIconId;

// Every code Open-Meteo's `weather_code` field can return — the full WMO
// "Weather interpretation codes" table from https://open-meteo.com/en/docs.
constexpr WeatherCodeEntry kTable[] = {
    {0, "Clear", "Clear Sky", Icon::ClearDay, Icon::ClearNight, kColourClear},
    {1, "Clear", "Mainly Clear", Icon::ClearDay, Icon::ClearNight, kColourClear},
    {2, "P.Cloudy", "Partly Cloudy", Icon::PartlyCloudyDay, Icon::PartlyCloudyNight,
     kColourCloudy},
    {3, "Overcast", "Overcast", Icon::Overcast, Icon::Overcast, kColourCloudy},
    {45, "Fog", "Fog", Icon::Fog, Icon::Fog, kColourFog},
    {48, "Fog", "Depositing Rime Fog", Icon::Fog, Icon::Fog, kColourFog},
    {51, "Drizzle", "Light Drizzle", Icon::Drizzle, Icon::Drizzle, kColourRain},
    {53, "Drizzle", "Moderate Drizzle", Icon::Drizzle, Icon::Drizzle, kColourRain},
    {55, "Drizzle", "Heavy Drizzle", Icon::Drizzle, Icon::Drizzle, kColourRain},
    {56, "Drizzle", "Light Freezing Drizzle", Icon::Drizzle, Icon::Drizzle, kColourRain},
    {57, "Drizzle", "Heavy Freezing Drizzle", Icon::Drizzle, Icon::Drizzle, kColourRain},
    {61, "Rain", "Light Rain", Icon::Rain, Icon::Rain, kColourRain},
    {63, "Rain", "Rain", Icon::Rain, Icon::Rain, kColourRain},
    {65, "Rain", "Heavy Rain", Icon::HeavyRain, Icon::HeavyRain, kColourRain},
    {66, "Rain", "Light Freezing Rain", Icon::Rain, Icon::Rain, kColourRain},
    {67, "Rain", "Heavy Freezing Rain", Icon::HeavyRain, Icon::HeavyRain, kColourRain},
    {71, "Snow", "Light Snow", Icon::Snow, Icon::Snow, kColourSnow},
    {73, "Snow", "Snow", Icon::Snow, Icon::Snow, kColourSnow},
    {75, "Snow", "Heavy Snow", Icon::HeavySnow, Icon::HeavySnow, kColourSnow},
    {77, "Snow", "Snow Grains", Icon::Snow, Icon::Snow, kColourSnow},
    {80, "Showers", "Rain Showers", Icon::Showers, Icon::Showers, kColourRain},
    {81, "Showers", "Rain Showers", Icon::Showers, Icon::Showers, kColourRain},
    {82, "Showers", "Heavy Rain Showers", Icon::HeavyRain, Icon::HeavyRain, kColourRain},
    {85, "Snow", "Snow Showers", Icon::Snow, Icon::Snow, kColourSnow},
    {86, "Snow", "Heavy Snow Showers", Icon::HeavySnow, Icon::HeavySnow, kColourSnow},
    {95, "Storm", "Thunderstorm", Icon::Thunderstorm, Icon::Thunderstorm, kColourStorm},
    {96, "Storm", "Thunderstorm (Hail)", Icon::Thunderstorm, Icon::Thunderstorm, kColourStorm},
    {99, "Storm", "Severe Thunderstorm", Icon::Thunderstorm, Icon::Thunderstorm, kColourStorm},
};
constexpr uint8_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

const WeatherCodeEntry* findEntry(int16_t code) {
  for (uint8_t i = 0; i < kTableSize; i++) {
    if (kTable[i].code == code) return &kTable[i];
  }
  return nullptr;
}

}  // namespace

const char* getWeatherDescription(int16_t code) {
  const WeatherCodeEntry* e = findEntry(code);
  return e != nullptr ? e->longDesc : "Unknown";
}

const char* getShortDescription(int16_t code) {
  const WeatherCodeEntry* e = findEntry(code);
  return e != nullptr ? e->shortDesc : "Unknown";
}

WeatherIconId getWeatherIcon(int16_t code, bool isDay) {
  const WeatherCodeEntry* e = findEntry(code);
  if (e == nullptr) return WeatherIconId::Unknown;
  return isDay ? e->dayIcon : e->nightIcon;
}

uint16_t getWeatherColour(int16_t code) {
  const WeatherCodeEntry* e = findEntry(code);
  return e != nullptr ? e->colour : kColourUnknown;
}
