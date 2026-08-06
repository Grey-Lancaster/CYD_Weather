#pragma once

#include <TFT_eSPI.h>

// A small, reusable line-graph renderer — grid + auto-scaled axis min/max
// labels + a smooth connected line, with graceful empty-data handling.
// Deliberately free functions (matching ui/Widgets.h's/WeatherIconTheme.h's
// existing convention), not a class: this is a stateless drawing helper, not
// something with its own lifecycle. Callers (e.g. pages/WeatherTrendsPage.h)
// own the data and decide when to redraw; this module only ever draws what
// it's handed, once per call — same "always full-redraw a changed region"
// pattern already used throughout src/pages/.
namespace ui {

// Computed from a data series so callers can show their own "current/min/
// max" callouts (e.g. in a page header) without recomputing anything
// themselves. `currentValue` is the most recent (last) point in the series.
struct GraphStats {
  bool hasData = false;
  float minValue = 0.0f;
  float maxValue = 0.0f;
  float currentValue = 0.0f;
};

GraphStats computeGraphStats(const float* values, size_t count);

// Visual knobs — pass the active Theme's own fields in, so this stays
// reusable across themes/pages rather than hardcoding one look.
struct GraphStyle {
  uint16_t lineColour;
  uint16_t gridColour;
  uint16_t labelColour;
  uint16_t bgColour;
  uint8_t font;  // TFT_eSPI built-in font id, used for the min/max labels
};

// Draws `count` chronological (oldest-first) values as a line graph filling
// the rect (x,y,w,h): background fill, a light horizontal grid, a smooth
// connected line, and min/max axis labels in the plot's top/bottom-right
// corners. Scales the vertical axis automatically to the data's own min/max
// (with a small guard against a perfectly flat series) — callers never need
// to pre-scale their data. If `count` < 2 there is nothing plottable, so
// this draws a centered "No data yet" message instead.
void drawLineGraph(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h, const float* values,
                   size_t count, const GraphStyle& style, const char* unitSuffix = "");

}  // namespace ui
