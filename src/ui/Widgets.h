#pragma once

#include <TFT_eSPI.h>

#include "config/AppConfig.h"
#include "display/DisplayManager.h"
#include "ui/Theme.h"

namespace ui {

// Shared page layout metrics (320x240, landscape).
namespace layout {
constexpr int32_t CARD_MARGIN_X = 8;
constexpr int32_t CARD_W = (int32_t)cfg::display::WIDTH - 2 * CARD_MARGIN_X;
constexpr int32_t CARD_H = 80;
constexpr int32_t CARD_GAP = 8;
constexpr int32_t CARD1_Y = (int32_t)cfg::ui::STATUS_BAR_H + CARD_GAP;
constexpr int32_t CARD2_Y = CARD1_Y + CARD_H + CARD_GAP;
constexpr int32_t ROWS_Y = 26;  // first key/value row offset inside a card
constexpr int32_t ROW_H = 16;
constexpr int32_t HINT_Y = (int32_t)cfg::display::HEIGHT - 8;  // hint baseline (BC datum)

// Layout for the 5-page dashboard (pages/CurrentWeatherPage.h and friends):
// a title line right below the status bar, a body area, and a page-indicator
// strip pinned to the bottom — every dashboard page keeps its content between
// BODY_Y and INDICATOR_Y so the indicator never gets overdrawn.
constexpr int32_t INDICATOR_H = 14;
constexpr int32_t INDICATOR_Y = (int32_t)cfg::display::HEIGHT - INDICATOR_H;
constexpr int32_t CONTENT_Y = (int32_t)cfg::ui::STATUS_BAR_H;
constexpr int32_t TITLE_H = 20;
constexpr int32_t TITLE_Y = CONTENT_Y + 4;
constexpr int32_t BODY_Y = CONTENT_Y + TITLE_H;
constexpr int32_t BODY_H = INDICATOR_Y - BODY_Y;
}  // namespace layout

// Page header used by every dashboard page — drawn once per full redraw.
inline void drawPageTitle(TFT_eSPI& gfx, const char* title) {
  const Theme& t = theme();
  gfx.setTextFont(t.fontSmall);
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.accent, t.bg);
  gfx.drawString(title, layout::CARD_MARGIN_X, layout::TITLE_Y);
}

// Small dot-style page indicator along the bottom strip. `current` and
// `total` count *rotatable* pages only (0-based current) — see
// PageManager::rotatablePosition(). Redraws the whole strip every call since
// it only runs as part of a full page repaint (never on a partial/dirty
// render), so there's no ghosting to worry about.
inline void drawPageIndicator(TFT_eSPI& gfx, uint8_t current, uint8_t total) {
  if (total == 0) return;
  const Theme& t = theme();
  constexpr int32_t kDotGap = 14;
  constexpr int32_t kDotROn = 3;
  constexpr int32_t kDotROff = 2;
  const int32_t cy = layout::INDICATOR_Y + layout::INDICATOR_H / 2;
  const int32_t spanW = (int32_t)(total - 1) * kDotGap;
  int32_t x = (int32_t)cfg::display::WIDTH / 2 - spanW / 2;
  gfx.fillRect(0, layout::INDICATOR_Y, cfg::display::WIDTH, layout::INDICATOR_H, t.bg);
  for (uint8_t i = 0; i < total; i++) {
    const bool active = (i == current);
    gfx.fillCircle(x, cy, active ? kDotROn : kDotROff, active ? t.accent : t.border);
    x += kDotGap;
  }
}

// Renders an arbitrary w x h region at (x, y). Uses the shared sprite canvas
// when available (flicker-free double buffer: the whole region is drawn
// off-screen, then pushed to the panel in one block transfer, instead of
// being erased and rebuilt piece-by-piece where it's visible), otherwise
// degrades to drawing directly on the TFT. The draw functor receives the
// target gfx and the region's origin *within that target* — (0,0) when a
// canvas was acquired (its own private coordinate space), or (x,y) in the
// direct-to-TFT fallback.
//
// w*h*2 (16bpp) must fit within cfg::display::CANVAS_MAX_BYTES — see
// DisplayManager::acquireCanvas(). The shared canvas is a single buffer
// reused (and reallocated on a size change) across every renderRegion()
// caller, so a page that redraws several same-size regions per frame (e.g.
// one call per card) only pays that reallocation cost once, the first time.
template <typename DrawFn>
inline void renderRegion(DisplayManager& display, int32_t x, int32_t y, uint16_t w, uint16_t h,
                         DrawFn drawFn) {
  TFT_eSprite* canvas = display.acquireCanvas(w, h);
  if (canvas != nullptr) {
    canvas->fillSprite(theme().bg);
    drawFn(*canvas, (int32_t)0, (int32_t)0);
    display.pushCanvas(x, y);
  } else {
    drawFn(display.tft(), x, y);
  }
}

// Convenience overload for a full-width horizontal strip at row `y`.
template <typename DrawFn>
inline void renderRegion(DisplayManager& display, int32_t y, uint16_t h, DrawFn drawFn) {
  renderRegion(display, (int32_t)0, y, cfg::display::WIDTH, h, drawFn);
}

inline void drawCard(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char* title) {
  const Theme& t = theme();
  gfx.fillRoundRect(x, y, w, h, t.cardRadius, t.surface);
  gfx.drawRoundRect(x, y, w, h, t.cardRadius, t.border);
  gfx.setTextFont(t.fontSmall);
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.accent, t.surface);
  gfx.drawString(title, x + t.pad, y + 6);
}

// Small triangle-with-"!" badge — the shared "offline"/stale-data indicator
// (see CurrentWeatherPage, HourlyForecastPage).
inline void drawWarningBadge(TFT_eSPI& gfx, int32_t cx, int32_t cy) {
  const Theme& t = theme();
  gfx.fillTriangle(cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, t.warn);
  gfx.setTextFont(t.fontSmall);
  gfx.setTextDatum(MC_DATUM);
  gfx.setTextColor(t.bg, t.warn);
  gfx.drawString("!", cx, cy + 1);
}

inline void drawKeyValue(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, const char* key,
                         const char* value) {
  const Theme& t = theme();
  gfx.setTextFont(t.fontSmall);
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.textSecondary, t.surface);
  gfx.drawString(key, x, y);
  gfx.setTextDatum(TR_DATUM);
  gfx.setTextColor(t.textPrimary, t.surface);
  gfx.drawString(value, x + w, y);
}

}  // namespace ui
