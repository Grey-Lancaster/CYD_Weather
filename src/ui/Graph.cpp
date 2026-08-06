#include "ui/Graph.h"

namespace ui {

GraphStats computeGraphStats(const float* values, size_t count) {
  GraphStats stats;
  if (values == nullptr || count == 0) return stats;
  stats.hasData = true;
  stats.minValue = values[0];
  stats.maxValue = values[0];
  for (size_t i = 1; i < count; i++) {
    if (values[i] < stats.minValue) stats.minValue = values[i];
    if (values[i] > stats.maxValue) stats.maxValue = values[i];
  }
  stats.currentValue = values[count - 1];
  return stats;
}

void drawLineGraph(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h, const float* values,
                   size_t count, const GraphStyle& style, const char* unitSuffix) {
  gfx.fillRect(x, y, w, h, style.bgColour);

  if (values == nullptr || count < 2) {
    gfx.setTextFont(style.font);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextColor(style.labelColour, style.bgColour);
    gfx.drawString("No data yet", x + w / 2, y + h / 2);
    return;
  }

  const GraphStats stats = computeGraphStats(values, count);
  float lo = stats.minValue;
  float hi = stats.maxValue;
  if (hi - lo < 0.5f) hi = lo + 0.5f;  // guards the divide below on flat/near-flat data

  // A light grid: 3 evenly spaced horizontal lines, drawn before the line
  // itself so the line always sits visually on top of it.
  constexpr uint8_t kGridLines = 3;
  for (uint8_t i = 1; i <= kGridLines; i++) {
    const int32_t gy = y + (int32_t)((float)i * (float)h / (float)(kGridLines + 1));
    gfx.drawFastHLine(x, gy, w, style.gridColour);
  }

  // "Smooth" here means a continuous connected line (vs. a stepped/discrete
  // bar chart) — straight segments between consecutive points, the same
  // technique already used by every other graph/sparkline in this codebase.
  int32_t prevX = 0;
  int32_t prevY = 0;
  for (size_t i = 0; i < count; i++) {
    const int32_t px = x + (int32_t)((float)i * (float)(w - 1) / (float)(count - 1));
    const int32_t py = y + h - 1 - (int32_t)(((values[i] - lo) / (hi - lo)) * (float)(h - 1));
    if (i > 0) gfx.drawLine(prevX, prevY, px, py, style.lineColour);
    prevX = px;
    prevY = py;
  }

  char hiStr[16];
  char loStr[16];
  snprintf(hiStr, sizeof(hiStr), "%.0f%s", (double)hi, unitSuffix);
  snprintf(loStr, sizeof(loStr), "%.0f%s", (double)lo, unitSuffix);
  gfx.setTextFont(style.font);
  gfx.setTextColor(style.labelColour, style.bgColour);
  gfx.setTextDatum(TR_DATUM);
  gfx.drawString(hiStr, x + w - 2, y + 2);
  gfx.setTextDatum(BR_DATUM);
  gfx.drawString(loStr, x + w - 2, y + h - 2);
}

}  // namespace ui
