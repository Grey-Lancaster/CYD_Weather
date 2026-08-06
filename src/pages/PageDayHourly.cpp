#include "pages/PageDayHourly.h"

#include <math.h>

#include "storage/SettingsManager.h"
#include "ui/WeatherIconTheme.h"
#include "ui/Widgets.h"
#include "weather/WeatherIcons.h"
#include "weather/WeatherUnits.h"

namespace {

// No dedicated header row — unlike the other pages' location/date (now
// redundant with the shared status bar, see ui/StatusBar.cpp), the day
// being viewed here genuinely isn't shown anywhere else (it can be any of
// the 5 forecast days, not just "today"), so it moves to the title row's
// right side instead of disappearing — see render(). Cards start right
// under the title (plus a small gap) and get the reclaimed height; the
// per-card offsets further down are proportional to it rather than fixed
// pixel counts, so they scale automatically if this ever changes again.
constexpr int32_t kTitleGap = 6;
constexpr int32_t kCardsY0 = ui::layout::BODY_Y + kTitleGap;
constexpr int32_t kCardsH = ui::layout::INDICATOR_Y - kCardsY0;
constexpr uint8_t kHeaderFont = 1;  // GLCD base font — compact, matches the other pages

// The shift interval itself is user-configurable
// (AppSettings::hourlyShiftS, web UI, same setting HourlyForecastPage.cpp
// uses) — not a constant here.
constexpr uint8_t kVisibleCards = 6;

constexpr int32_t kGap = 4;
constexpr int32_t kCardW = (ui::layout::CARD_W - (int32_t)(kVisibleCards - 1) * kGap) / kVisibleCards;
constexpr int32_t kRadius = 8;
constexpr int32_t kIconR = 15;

// Proportional to kCardsH (scaled from the original fixed-pixel values,
// tuned for an 18px-shorter card) — see HourlyForecastPage.cpp's identical
// comment.
constexpr int32_t kTimeOffsetY = kCardsH * 16 / 164;
constexpr int32_t kIconOffsetY = kCardsH * 50 / 164;
constexpr int32_t kTempOffsetY = kCardsH * 82 / 164;
constexpr int32_t kRainOffsetY = kCardsH * 100 / 164;
constexpr int32_t kWindOffsetY = kCardsH * 116 / 164;

// This page is non-rotatable (see PageDayHourly.h), so PageManager never
// draws the page-indicator dot strip while it's active — safe to use that
// same bottom band for a "how to get back" hint instead.
constexpr int32_t kHintY = ui::layout::INDICATOR_Y + ui::layout::INDICATOR_H / 2;

int32_t cardX(uint8_t slot) {
  return ui::layout::CARD_MARGIN_X + (int32_t)slot * (kCardW + kGap);
}

// Same blend/gradient approach as ForecastPage/HourlyForecastPage's cards —
// duplicated rather than shared, matching this project's convention of
// small, self-contained per-page anonymous-namespace helpers.
uint16_t blend565(uint16_t a, uint16_t b, float t) {
  const int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int32_t r = ar + (int32_t)((br - ar) * t);
  const int32_t g = ag + (int32_t)((bg - ag) * t);
  const int32_t bl = ab + (int32_t)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Every card gets the same visible lighter-top/darker-bottom fade;
// `highlight` (soonest hour, day 0 only) layers an accent tint on top of
// *that same* gradient rather than swapping to a different top colour — see
// ForecastPage.cpp's identical fix for why (a plain `t.surface` top with no
// blend was the previous non-highlighted behaviour, i.e. no visible fade).
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

// Draws at an explicit (x, y) origin rather than computing it from a card
// index — see HourlyForecastPage.cpp's identical helper for why (it's what
// lets render() double-buffer each card through ui::renderRegion() instead
// of a visible erase-then-rebuild flicker).
void drawHourlyCard(TFT_eSPI& gfx, int32_t x, int32_t y, const HourlyForecastEntry& h,
                    bool highlight, TempUnit tempUnit, WindUnit windUnit) {
  const Theme& t = theme();
  const int32_t cx = x + kCardW / 2;

  drawHourlyCardBackground(gfx, x, y, kCardW, kCardsH, highlight);

  // Transparent text throughout (see ForecastPage.cpp for why): the
  // background is a gradient, freshly repainted immediately above.
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

// [startIdx, startIdx+count) within d.hourly for `dayIndex` (0 = today).
// Day boundaries are "00:00" entries; day 0 always starts at index 0
// regardless of what hour "now" actually is (even if that's itself 00:xx),
// so boundary-hunting starts from index 1, never index 0 — otherwise a
// fetch made right after local midnight would misread hourly[0]'s own
// "00:00" as the end of day 0 instead of its start.
void hourRangeForDay(const WeatherData& d, uint8_t dayIndex, uint8_t& startIdx, uint8_t& count) {
  uint8_t midnightsSeen = 0;
  uint8_t start = 0;
  bool foundStart = (dayIndex == 0);
  for (uint8_t i = 1; i < d.hourlyCount; i++) {
    if (strcmp(d.hourly[i].time, "00:00") != 0) continue;
    midnightsSeen++;
    if (midnightsSeen == dayIndex) {
      start = i;
      foundStart = true;
    } else if (midnightsSeen == (uint8_t)(dayIndex + 1)) {
      startIdx = start;
      count = (uint8_t)(i - start);
      return;
    }
  }
  if (!foundStart) {
    startIdx = 0;
    count = 0;
    return;
  }
  startIdx = start;
  count = d.hourlyCount > start ? (uint8_t)(d.hourlyCount - start) : (uint8_t)0;
}

}  // namespace

PageDayHourly::PageDayHourly(DisplayManager& display, WeatherService& weather)
    : _display(display), _weather(weather) {}

void PageDayHourly::onEnter() {
  Page::onEnter();
  // A previous visit may have left the window scrolled mid-way through a
  // *different* day — always start a fresh day at its first visible hour.
  _windowStart = 0;
  _lastShiftMs = millis();
}

bool PageDayHourly::handleTap(int16_t, int16_t) {
  if (_onBack) _onBack();
  return true;  // always consumed — see class comment in PageDayHourly.h
}

void PageDayHourly::tick(uint32_t nowMs) {
  const uint32_t version = _weather.dataVersion();
  const bool failed = _weather.lastFetchFailed();
  bool dirty = false;

  if (version != _lastSeenVersion || failed != _lastSeenFailed) {
    _lastSeenVersion = version;
    _lastSeenFailed = failed;
    _windowStart = 0;  // fresh data — the day's hour range may have shifted
    _lastShiftMs = nowMs;
    dirty = true;
  }

  const WeatherData d = _weather.data();
  uint8_t start = 0, count = 0;
  if (d.valid) hourRangeForDay(d, _dayIndex, start, count);
  const uint32_t shiftIntervalMs =
      (uint32_t)SettingsManager::instance().get().hourlyShiftS * 1000u;
  const uint8_t maxStart = count > kVisibleCards ? (uint8_t)(count - kVisibleCards) : 0;
  if (maxStart > 0 && nowMs - _lastShiftMs >= shiftIntervalMs) {
    _lastShiftMs = nowMs;
    _windowStart = (uint8_t)((_windowStart + 1) % (maxStart + 1));
    dirty = true;
  }

  if (dirty) requestRedraw();
}

void PageDayHourly::render(bool full) {
  // Deliberately no upfront full-body fillRect — every card below is fully
  // opaque and redraws its own cell exactly, so clearing the row first only
  // adds a visible blank flash before the cards reappear (the "whole screen
  // redraws/blinks" effect). See HourlyForecastPage.cpp's render() for the
  // same fix and why an off-screen-buffered version of this (full double
  // buffering via ui::renderRegion()) was tried and reverted — it caused an
  // intermittent hang on real hardware.
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const WeatherData d = _weather.data();
  const bool offline = _weather.lastFetchFailed();
  const AppSettings& settings = SettingsManager::instance().get();
  const auto tempUnit = static_cast<TempUnit>(settings.tempUnit);
  const auto windUnit = static_cast<WindUnit>(settings.windUnit);

  tft.fillRect(0, ui::layout::INDICATOR_Y, cfg::display::WIDTH, ui::layout::INDICATOR_H, t.bg);
  ui::drawPageTitle(tft, "Hourly Forecast");

  // Which of the 5 days this is — on the title row's right side (location
  // and "today"'s date now live in the shared status bar, see
  // ui/StatusBar.cpp, but *this* is the one thing only this page knows).
  const bool haveDay = d.valid && _dayIndex < d.dailyCount;
  char header[48];
  if (haveDay) {
    snprintf(header, sizeof(header), "%s, %s", d.daily[_dayIndex].dayLabel,
             d.daily[_dayIndex].dateLabel);
  } else {
    strlcpy(header, d.valid ? "Day not available" : "Fetching forecast...", sizeof(header));
  }
  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(header, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W, ui::layout::TITLE_Y);
  if (offline) {
    const int32_t headerW = tft.textWidth(header, t.fontSmall);
    ui::drawWarningBadge(tft, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W - headerW - 14,
                         ui::layout::TITLE_Y + 7);
  }

  // "< tap to return" hint, where the page-indicator dots would normally
  // sit for a rotatable page (see kHintY's comment).
  tft.setTextFont(kHeaderFont);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString("Tap anywhere to return to 5 Day Forecast", cfg::display::WIDTH / 2, kHintY);

  uint8_t start = 0, count = 0;
  if (haveDay) hourRangeForDay(d, _dayIndex, start, count);

  if (!haveDay || count == 0) {
    tft.fillRect(0, kCardsY0, cfg::display::WIDTH, kCardsH, t.bg);
    const char* msg = !d.valid ? (offline ? "No data (offline)" : "Fetching...") : "No hourly data for this day";
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString(msg, cfg::display::WIDTH / 2, kCardsY0 + kCardsH / 2);
    return;
  }

  const uint8_t windowStart = min(_windowStart, (uint8_t)(count > 0 ? count - 1 : 0));
  const uint8_t visible = min((uint8_t)(count - windowStart), kVisibleCards);
  for (uint8_t slot = 0; slot < visible; slot++) {
    const uint8_t hourIndex = (uint8_t)(start + windowStart + slot);
    // "Now" only means something on day 0's very first hour — every hour on
    // a future day is equally "not yet happened", so nothing is highlighted
    // there.
    const bool highlight = _dayIndex == 0 && hourIndex == 0;
    drawHourlyCard(tft, cardX(slot), kCardsY0, d.hourly[hourIndex], highlight, tempUnit, windUnit);
  }
  if (visible < kVisibleCards) {
    const int32_t clearX = cardX(visible);
    tft.fillRect(clearX, kCardsY0, cfg::display::WIDTH - clearX, kCardsH, t.bg);
  }
}
