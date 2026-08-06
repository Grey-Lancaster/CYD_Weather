#include "pages/HourlyForecastPage.h"

#include <math.h>

#include "storage/SettingsManager.h"
#include "ui/WeatherIconTheme.h"
#include "ui/Widgets.h"
#include "weather/WeatherIcons.h"
#include "weather/WeatherUnits.h"

namespace {

// No dedicated header row — location now lives in the shared status bar
// (see ui/StatusBar.cpp), and "Updated HH:MM" sits on the title row's right
// side instead (see render()), same treatment as ForecastPage.cpp. Cards
// start right under the title (plus a small gap) and get the reclaimed
// height; the per-card offsets further down are proportional to it rather
// than fixed pixel counts, so they scale automatically if this ever
// changes again.
constexpr int32_t kTitleGap = 6;
constexpr int32_t kCardsY0 = ui::layout::BODY_Y + kTitleGap;
constexpr int32_t kCardsH = ui::layout::INDICATOR_Y - kCardsY0;
constexpr uint8_t kHeaderFont = 1;  // GLCD base font — compact, matches the other pages

// Only this many hours are shown at once (12 would be unreadably narrow) —
// the visible window shifts forward through all 12 over time, see tick().
// The shift interval itself is user-configurable (AppSettings::hourlyShiftS,
// web UI), not a constant here.
constexpr uint8_t kVisibleCards = 6;

constexpr int32_t kGap = 4;
constexpr int32_t kCardW = (ui::layout::CARD_W - (int32_t)(kVisibleCards - 1) * kGap) / kVisibleCards;
constexpr int32_t kRadius = 8;
constexpr int32_t kIconR = 15;

// Per-card content offsets, relative to the card's top edge — proportional
// to kCardsH (scaled from the original fixed-pixel values, tuned for an
// 18px-shorter card) so the cards' new extra height spreads the content out
// rather than leaving it bunched at the top with blank space below. One row
// shorter than ForecastPage's cards (no separate day+date row, just a
// single time label), so this is a timeline, not a copy of the 5-day layout.
constexpr int32_t kTimeOffsetY = kCardsH * 16 / 164;
constexpr int32_t kIconOffsetY = kCardsH * 50 / 164;
constexpr int32_t kTempOffsetY = kCardsH * 82 / 164;
constexpr int32_t kRainOffsetY = kCardsH * 100 / 164;
constexpr int32_t kWindOffsetY = kCardsH * 116 / 164;

int32_t cardX(uint8_t slot) {
  return ui::layout::CARD_MARGIN_X + (int32_t)slot * (kCardW + kGap);
}

// Identical blend/gradient approach to ForecastPage's cards (same visual
// design language, per this page's brief) — duplicated rather than shared,
// matching this project's convention of small, self-contained per-page
// anonymous-namespace helpers.
uint16_t blend565(uint16_t a, uint16_t b, float t) {
  const int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int32_t r = ar + (int32_t)((br - ar) * t);
  const int32_t g = ag + (int32_t)((bg - ag) * t);
  const int32_t bl = ab + (int32_t)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Every card gets the same visible lighter-top/darker-bottom fade;
// `highlight` (soonest hour) layers an accent tint on top of *that same*
// gradient rather than swapping to a different top colour, so it reads as
// "the same style, emphasised" rather than a visually distinct/odd-looking
// card next to flat ones — see ForecastPage.cpp's identical fix for why (a
// plain `t.surface` top with no blend was the previous non-highlighted
// behaviour, i.e. no visible fade at all).
void drawHourlyCardBackground(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h,
                              bool highlight) {
  const Theme& t = theme();
  const uint16_t baseTop = blend565(t.surface, t.textPrimary, 0.08f);
  const uint16_t baseBottom = blend565(t.surface, t.bg, 0.5f);
  const uint16_t topColour = highlight ? blend565(baseTop, t.accent, 0.25f) : baseTop;
  const uint16_t bottomColour = highlight ? blend565(baseBottom, t.accent, 0.12f) : baseBottom;

  // No fillRoundRect for the fill — a flat base colour there (whichever end
  // of the gradient, or the midpoint between them) always reads as a seam
  // against the true gradient band right next to it, since the two
  // kRadius-tall rounded-corner zones can't be covered by a plain
  // horizontal band without its square ends poking out past the curve. So
  // every row is drawn as its own thin strip in that row's exact gradient
  // colour instead; corner rows are additionally inset by the same circle
  // math a rounded rect's curve follows (radius r, row `dy` from the
  // corner's centre -> inset = r - sqrt(r^2 - dy^2)) so the silhouette
  // still reads as properly rounded — see ForecastPage.cpp's identical fix.
  const float fDenom = (float)(h > 1 ? h - 1 : 1);
  for (int32_t row = 0; row < kRadius; row++) {
    const int32_t dy = kRadius - row;
    const int32_t inset = kRadius - (int32_t)sqrtf((float)(kRadius * kRadius - dy * dy));
    const float fTop = (float)row / fDenom;
    const float fBottom = (float)(h - 1 - row) / fDenom;
    gfx.drawFastHLine(x + inset, y + row, w - 2 * inset, blend565(topColour, bottomColour, fTop));
    gfx.drawFastHLine(x + inset, y + h - 1 - row, w - 2 * inset,
                      blend565(topColour, bottomColour, fBottom));
  }
  const int32_t bandY0 = y + kRadius;
  const int32_t bandRegionH = h - 2 * kRadius;
  const int32_t bandH = 3;
  const int32_t kBands = bandRegionH > 0 ? max((int32_t)1, bandRegionH / bandH) : 0;
  for (int32_t i = 0; i < kBands; i++) {
    const int32_t rowY = bandY0 + i * bandH;
    // Last band absorbs bandRegionH's remainder (rarely divides evenly by
    // bandH) rather than the loop stopping short and leaving a sliver of
    // the previous frame showing right at the bottom corner zone.
    const int32_t thisBandH = (i == kBands - 1) ? (bandRegionH - i * bandH) : bandH;
    const float f = (float)(rowY - y) / fDenom;
    gfx.fillRect(x, rowY, w, thisBandH, blend565(topColour, bottomColour, f));
  }
  const uint16_t borderColour = highlight ? t.accent : t.border;
  gfx.drawRoundRect(x, y, w, h, kRadius, borderColour);
  if (highlight) gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, kRadius - 1, borderColour);
}

// `highlight` marks the soonest hour ("now") — same idea as ForecastPage
// highlighting today, applied to the hourly timeline instead. Draws at an
// explicit (x, y) origin rather than computing it from a card index, so the
// same function works whether `gfx` is the real TFT at (cardX(slot),
// kCardsY0) or a private off-screen canvas at (0, 0) — see render()'s use
// of ui::renderRegion(), which is what makes each card's redraw appear
// atomically instead of a visible erase-then-rebuild flicker.
void drawHourlyCard(TFT_eSPI& gfx, int32_t x, int32_t y, const HourlyForecastEntry& h,
                    bool highlight, TempUnit tempUnit, WindUnit windUnit) {
  const Theme& t = theme();
  const int32_t cx = x + kCardW / 2;

  drawHourlyCardBackground(gfx, x, y, kCardW, kCardsH, highlight);

  // Transparent text throughout (see ForecastPage.cpp for why): the
  // background is a gradient, freshly repainted immediately above, so an
  // opaque background parameter would paint a mismatched solid box.
  // Time uses the compact font, not fontSmall — these cards are narrower
  // than ForecastPage's (6 per row instead of 5), and "HH:MM" is the widest
  // string any card here needs to show; temperature keeps fontSmall as the
  // one visually-prominent readout per card.
  gfx.setTextFont(kHeaderFont);
  gfx.setTextDatum(TC_DATUM);
  gfx.setTextColor(highlight ? t.accent : t.textPrimary);
  gfx.drawString(h.time, cx, y + kTimeOffsetY);

  const WeatherIconId icon = getWeatherIcon(h.weatherCode, h.isDay);
  ui::drawWeatherIcon(gfx, cx, y + kIconOffsetY, kIconR, icon);

  char temp[8];
  char rain[8];
  char wind[10];
  snprintf(temp, sizeof(temp), "%ld%s", lroundf(convertTempC(h.temperatureC, tempUnit)),
           tempUnitSuffix(tempUnit));
  snprintf(rain, sizeof(rain), "%d%%", (int)h.rainProbabilityPct);
  snprintf(wind, sizeof(wind), "%ld%s", lroundf(convertWindKmh(h.windSpeedKmh, windUnit)),
           windUnitSuffix(windUnit));

  gfx.setTextFont(t.fontSmall);
  gfx.setTextColor(t.textPrimary);
  gfx.drawString(temp, cx, y + kTempOffsetY);

  gfx.setTextFont(kHeaderFont);
  gfx.setTextColor(t.textSecondary);
  gfx.drawString(rain, cx, y + kRainOffsetY);
  gfx.drawString(wind, cx, y + kWindOffsetY);
}

}  // namespace

HourlyForecastPage::HourlyForecastPage(DisplayManager& display, WeatherService& weather)
    : _display(display), _weather(weather) {}

void HourlyForecastPage::tick(uint32_t nowMs) {
  const uint32_t version = _weather.dataVersion();
  const bool failed = _weather.lastFetchFailed();
  bool dirty = false;

  if (version != _lastSeenVersion || failed != _lastSeenFailed) {
    _lastSeenVersion = version;
    _lastSeenFailed = failed;
    // One data() copy only when something actually changed — cheap enough
    // (this only happens on a fetch landing, not every tick) and avoids the
    // mutex-guarded full-struct copy on every tick just to read a count.
    _hourlyCount = _weather.data().hourlyCount;
    _windowStart = 0;  // fresh data — restart the timeline from "now"
    _lastShiftMs = nowMs;
    dirty = true;
  }

  const uint32_t shiftIntervalMs =
      (uint32_t)SettingsManager::instance().get().hourlyShiftS * 1000u;
  const uint8_t maxStart = _hourlyCount > kVisibleCards ? _hourlyCount - kVisibleCards : 0;
  if (maxStart > 0 && nowMs - _lastShiftMs >= shiftIntervalMs) {
    _lastShiftMs = nowMs;
    _windowStart = (uint8_t)((_windowStart + 1) % (maxStart + 1));
    dirty = true;
  }

  if (dirty) requestRedraw();
}

void HourlyForecastPage::render(bool full) {
  // A fetch landing (or the window shifting) redraws via requestRedraw() ->
  // render(false) (see tick()). Deliberately *not* one big fillRect over the
  // whole body first: every card below is fully opaque and redraws its own
  // cell exactly (background, gradient, text all repainted), so clearing
  // the row first only adds a visible blank flash between the clear and the
  // cards reappearing — the "whole screen redraws/blinks" effect. Skipping
  // it means old card content is overpainted directly by new card content,
  // with no plain-background frame in between.
  //
  // (An off-screen-buffered version of this — full double buffering via
  // ui::renderRegion(), one canvas per card — was tried and reverted: it
  // caused an intermittent hang on real hardware, most likely from resizing
  // the shared sprite canvas mid-page-transition. See project memory before
  // reaching for that again.)
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const WeatherData d = _weather.data();
  const bool offline = _weather.lastFetchFailed();
  const AppSettings& settings = SettingsManager::instance().get();
  const auto tempUnit = static_cast<TempUnit>(settings.tempUnit);
  const auto windUnit = static_cast<WindUnit>(settings.windUnit);

  ui::drawPageTitle(tft, "Hourly Forecast");

  // "Updated HH:MM" (or a fetching/offline status) on the title row's right
  // side — location used to live in a dedicated header row below the
  // title, but it now lives in the shared status bar (see
  // ui/StatusBar.cpp), so this is the only per-fetch status this page still
  // needs to show. Same treatment as ForecastPage.cpp.
  char updated[24];
  if (d.valid) {
    snprintf(updated, sizeof(updated), "Updated %s", d.lastUpdate);
  } else {
    strlcpy(updated, offline ? "No data (offline)" : "Fetching...", sizeof(updated));
  }
  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(updated, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W, ui::layout::TITLE_Y);
  if (offline) {
    const int32_t updatedW = tft.textWidth(updated, t.fontSmall);
    ui::drawWarningBadge(tft, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W - updatedW - 14,
                         ui::layout::TITLE_Y + 7);
  }

  if (!d.valid || d.hourlyCount == 0) {
    tft.fillRect(0, kCardsY0, cfg::display::WIDTH, kCardsH, t.bg);
    const char* msg = d.valid ? "No hourly data" : (offline ? "No data (offline)" : "Fetching...");
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString(msg, cfg::display::WIDTH / 2, kCardsY0 + kCardsH / 2);
    return;
  }

  const uint8_t total = min(d.hourlyCount, cfg::weather::HOURLY_DISPLAY_COUNT);
  const uint8_t start = min(_windowStart, (uint8_t)(total > 0 ? total - 1 : 0));
  const uint8_t visible = min((uint8_t)(total - start), kVisibleCards);
  for (uint8_t slot = 0; slot < visible; slot++) {
    const uint8_t hourIndex = start + slot;
    drawHourlyCard(tft, cardX(slot), kCardsY0, d.hourly[hourIndex], /*highlight=*/hourIndex == 0,
                  tempUnit, windUnit);
  }
  if (visible < kVisibleCards) {
    // Fewer cards than last time (rare — only if hourlyCount shrinks between
    // fetches) — clear the now-unused trailing slots directly so they don't
    // keep showing stale cards from the previous render.
    const int32_t clearX = cardX(visible);
    tft.fillRect(clearX, kCardsY0, cfg::display::WIDTH - clearX, kCardsH, t.bg);
  }
}
