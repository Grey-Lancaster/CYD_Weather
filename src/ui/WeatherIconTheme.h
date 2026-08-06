#pragma once

#include <TFT_eSPI.h>

#include "weather/WeatherIcons.h"

// Draws the bitmap weather icon art (ui/WeatherIconBitmaps.h). Kept in its
// own file — separate from the general-purpose widgets in ui/Widgets.h and
// from every page's drawing code — specifically so a future icon theme (a
// different style) is a matter of swapping this one file's implementation;
// callers only ever depend on WeatherIconId (weather/WeatherIcons.h), never
// on how a theme draws it.
namespace ui {

// Draws `icon` centered at (cx, cy); `r` picks the small (32x32) or large
// (44x44) pre-rendered bitmap tier, whichever is the closer fit.
void drawWeatherIcon(TFT_eSPI& gfx, int32_t cx, int32_t cy, int32_t r, WeatherIconId icon);

}  // namespace ui
