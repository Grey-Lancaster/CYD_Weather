#pragma once

#include <TFT_eSPI.h>

#include "weather/WeatherAlertManager.h"

// Severity colour/icon lookup, shared by ui/StatusBar.cpp (the top banner)
// and pages/PageAlerts.cpp (the alert list) so both draw severity
// identically — mirrors the weather/WeatherIcons.h + ui/WeatherIconTheme.h
// split already used for weather condition icons.
//
// No emoji: the built-in TFT_eSPI fonts are ASCII bitmap fonts with no
// Unicode glyphs, so severity is communicated with colour plus a simple
// drawn shape instead of the "⚠"/"❄" characters in the brief's examples.
namespace ui {

uint16_t alertSeverityColour(AlertSeverity severity);

// Draws a small severity glyph centred at (cx, cy) with radius-ish size r:
// Danger = filled triangle + "!", Warning = outlined triangle + "!",
// Advisory = filled circle + "i", Information = outlined circle + dot.
void drawAlertIcon(TFT_eSPI& gfx, int32_t cx, int32_t cy, int32_t r, AlertSeverity severity);

}  // namespace ui
