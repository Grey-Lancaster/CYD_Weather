#include "ui/WeatherIconTheme.h"

#include "ui/WeatherIconBitmaps.h"

namespace {

// Below this icon "radius" the small (32x32) tier is used; at/above it, the
// large (44x44) tier — matches the two sizes every current caller actually
// asks for (18/15/15 vs 26/28, see pages/ForecastPage.cpp,
// pages/CurrentConditionsPage.cpp, pages/HourlyForecastPage.cpp,
// pages/PageDayHourly.cpp). Picking the closer pre-rendered bitmap rather
// than runtime-scaling a single one keeps every icon pixel-crisp with no
// scaling blur — see ui/WeatherIconBitmaps.h.
constexpr int32_t kSizeThresholdR = 22;

struct IconBitmap {
  const uint16_t* small;
  const uint16_t* large;
};

// Indexed by WeatherIconId (weather/WeatherIcons.h) — order must match that
// enum exactly.
constexpr IconBitmap kBitmaps[] = {
    {ui::kIcon32_ClearDay, ui::kIcon44_ClearDay},
    {ui::kIcon32_ClearNight, ui::kIcon44_ClearNight},
    {ui::kIcon32_PartlyCloudyDay, ui::kIcon44_PartlyCloudyDay},
    {ui::kIcon32_PartlyCloudyNight, ui::kIcon44_PartlyCloudyNight},
    {ui::kIcon32_Cloudy, ui::kIcon44_Cloudy},
    {ui::kIcon32_Overcast, ui::kIcon44_Overcast},
    {ui::kIcon32_Fog, ui::kIcon44_Fog},
    {ui::kIcon32_Drizzle, ui::kIcon44_Drizzle},
    {ui::kIcon32_Rain, ui::kIcon44_Rain},
    {ui::kIcon32_HeavyRain, ui::kIcon44_HeavyRain},
    {ui::kIcon32_Showers, ui::kIcon44_Showers},
    {ui::kIcon32_Thunderstorm, ui::kIcon44_Thunderstorm},
    {ui::kIcon32_Snow, ui::kIcon44_Snow},
    {ui::kIcon32_HeavySnow, ui::kIcon44_HeavySnow},
    {ui::kIcon32_Unknown, ui::kIcon44_Unknown},
};
constexpr size_t kBitmapCount = sizeof(kBitmaps) / sizeof(kBitmaps[0]);

}  // namespace

namespace ui {

void drawWeatherIcon(TFT_eSPI& gfx, int32_t cx, int32_t cy, int32_t r, WeatherIconId icon) {
  const size_t index = (size_t)icon;
  const IconBitmap& bmp = index < kBitmapCount ? kBitmaps[index] : kBitmaps[(size_t)WeatherIconId::Unknown];
  const bool large = r >= kSizeThresholdR;
  const uint16_t* data = large ? bmp.large : bmp.small;
  const int32_t size = large ? kIconSizeLarge : kIconSizeSmall;
  // pushImage() streams raw uint16_t values as-is; the RGB565 words in
  // ui/WeatherIconBitmaps.h were packed MSB-first (standard bit-shift
  // packing) but the ESP32 stores a uint16_t little-endian in RAM, so
  // without this the panel reads every colour byte-swapped — including the
  // transparency key itself, which is why the untreated bug showed a solid
  // cyan box instead of a transparent one (0xF81F swapped is 0x1FF8, a
  // bright cyan). No other code in this project uses pushImage from a raw
  // array, so it's safe to leave this flag set only for the duration of
  // this call rather than threading state through every caller.
  gfx.setSwapBytes(true);
  gfx.pushImage(cx - size / 2, cy - size / 2, size, size, data, kIconTransparentKey);
  gfx.setSwapBytes(false);
}

}  // namespace ui
