#include "pages/ForecastPage.h"

#include <math.h>

#include "storage/SettingsManager.h"
#include "ui/WeatherIconTheme.h"
#include "ui/Widgets.h"
#include "weather/WeatherIcons.h"
#include "weather/WeatherUnits.h"

namespace {

// No dedicated header row any more — location now lives in the shared
// status bar (see ui/StatusBar.cpp), and "Updated HH:MM" sits on the title
// row's right side instead (see render()). The cards still get almost all
// of the reclaimed height (see kCardsH below, and the offsets further down,
// which are proportional to it rather than fixed pixel counts, so they
// scale automatically if this ever changes again) — just with a small gap
// first so they don't sit flush against the title text.
constexpr int32_t kTitleGap = 6;
constexpr int32_t kCardsY0 = ui::layout::BODY_Y + kTitleGap;
constexpr int32_t kCardsH = ui::layout::INDICATOR_Y - kCardsY0;
constexpr uint8_t kHeaderFont = 1;  // GLCD base font — compact, matches HourlyForecastPage

constexpr uint8_t kCards = cfg::weather::DAILY_FETCH_COUNT;
constexpr int32_t kGap = 4;
constexpr int32_t kCardW = (ui::layout::CARD_W - (int32_t)(kCards - 1) * kGap) / kCards;
constexpr int32_t kRadius = 8;
constexpr int32_t kIconR = 22;  // >= ui/WeatherIconTheme.cpp's size threshold -> the larger 44px bitmap tier

// Per-card content offsets, relative to the card's top edge — proportional
// to kCardsH (scaled from the original fixed-pixel values, tuned when this
// page had an 18px-shorter card) so the cards' new extra height spreads the
// content out rather than leaving it bunched at the top with blank space
// below.
constexpr int32_t kDayOffsetY = kCardsH * 16 / 164;
constexpr int32_t kIconOffsetY = kCardsH * 52 / 164;
constexpr int32_t kHighOffsetY = kCardsH * 86 / 164;
constexpr int32_t kLowOffsetY = kCardsH * 104 / 164;
constexpr int32_t kRainOffsetY = kCardsH * 124 / 164;
constexpr int32_t kWindOffsetY = kCardsH * 142 / 164;

int32_t cardX(uint8_t i) {
  return ui::layout::CARD_MARGIN_X + (int32_t)i * (kCardW + kGap);
}

// Blends two RGB565 colours (5/6/5-bit component lerp — cheap, and plenty
// smooth enough for the "subtle gradient" this card background wants).
uint16_t blend565(uint16_t a, uint16_t b, float t) {
  const int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int32_t r = ar + (int32_t)((br - ar) * t);
  const int32_t g = ag + (int32_t)((bg - ag) * t);
  const int32_t bl = ab + (int32_t)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Rounded card with a soft vertical gradient fill (banded — TFT_eSPI has no
// native gradient primitive) and a border. Every card gets the same visible
// lighter-top/darker-bottom fade — `highlight` (today) layers an accent
// tint on top of *that same* gradient rather than swapping to a completely
// different top colour, so the highlighted card reads as "the same style,
// emphasised" instead of a visually distinct/odd-looking tile next to flat
// ones (a plain `t.surface` top with no blend at all was the previous
// non-highlighted behaviour — effectively no visible fade, which is why the
// gradient only seemed to exist on the highlighted card).
void drawForecastCardBackground(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h,
                                bool highlight) {
  const Theme& t = theme();
  const uint16_t baseTop = blend565(t.surface, t.textPrimary, 0.08f);
  const uint16_t baseBottom = blend565(t.surface, t.bg, 0.5f);
  const uint16_t topColour = highlight ? blend565(baseTop, t.accent, 0.25f) : baseTop;
  const uint16_t bottomColour = highlight ? blend565(baseBottom, t.accent, 0.12f) : baseBottom;

  // A plain fillRoundRect base fill can only take one flat colour for the
  // whole shape, but the two rounded-corner zones (top kRadius rows, bottom
  // kRadius rows — a horizontal band can't cover them without its square
  // ends poking out past the curve) sit right next to gradient bands that
  // are close to the *opposite* end of the range. Whichever single colour
  // gets picked for that flat base, one corner (or, split down the middle,
  // both corners a little) reads as a visible seam against the true
  // gradient colour beside it — there's no flat-fill colour that fixes
  // this, only computing the corners *as* gradient too.
  //
  // So: no fillRoundRect for the fill at all. Every row is drawn as its own
  // thin strip in that row's exact gradient colour; the corner rows are
  // additionally inset by the same circle math a rounded rect's curve
  // follows (radius r, row `dy` from the corner's centre -> inset =
  // r - sqrt(r^2 - dy^2)), so the silhouette still reads as properly
  // rounded, just filled correctly instead of with one compromise colour.
  const float fDenom = (float)(h > 1 ? h - 1 : 1);
  for (int32_t row = 0; row < kRadius; row++) {
    const int32_t dy = kRadius - row;  // rows above the corner circle's centre
    const int32_t inset = kRadius - (int32_t)sqrtf((float)(kRadius * kRadius - dy * dy));
    const float fTop = (float)row / fDenom;
    const float fBottom = (float)(h - 1 - row) / fDenom;
    gfx.drawFastHLine(x + inset, y + row, w - 2 * inset, blend565(topColour, bottomColour, fTop));
    gfx.drawFastHLine(x + inset, y + h - 1 - row, w - 2 * inset,
                      blend565(topColour, bottomColour, fBottom));
  }
  // Interior bands (~3px thick — a fixed band count looked visibly
  // "stepped" once these cards grew taller) — same f = row/(h-1) formula as
  // the corner rows above, so the two meet with no colour discontinuity at
  // the seam. bandRegionH rarely divides evenly by bandH; the last band
  // absorbs whatever's left over (rather than the loop just stopping short)
  // so there's no uncovered sliver of the *previous* frame's pixels left
  // showing right where this region meets the bottom corner zone.
  const int32_t bandY0 = y + kRadius;
  const int32_t bandRegionH = h - 2 * kRadius;
  const int32_t bandH = 3;
  const int32_t kBands = bandRegionH > 0 ? max((int32_t)1, bandRegionH / bandH) : 0;
  for (int32_t i = 0; i < kBands; i++) {
    const int32_t rowY = bandY0 + i * bandH;
    const int32_t thisBandH = (i == kBands - 1) ? (bandRegionH - i * bandH) : bandH;
    const float f = (float)(rowY - y) / fDenom;
    gfx.fillRect(x, rowY, w, thisBandH, blend565(topColour, bottomColour, f));
  }
  const uint16_t borderColour = highlight ? t.accent : t.border;
  gfx.drawRoundRect(x, y, w, h, kRadius, borderColour);
  if (highlight) gfx.drawRoundRect(x + 1, y + 1, w - 2, h - 2, kRadius - 1, borderColour);
}

void drawForecastCard(TFT_eSPI& gfx, uint8_t index, const DailyForecastEntry& d, bool highlight,
                     TempUnit tempUnit, WindUnit windUnit) {
  const Theme& t = theme();
  const int32_t x = cardX(index);
  const int32_t cx = x + kCardW / 2;

  drawForecastCardBackground(gfx, x, kCardsY0, kCardW, kCardsH, highlight);

  // Transparent text (single-arg setTextColor) throughout this card: the
  // background is a gradient, not a flat colour, so an opaque background
  // parameter here would paint a mismatched solid box over it (this was a
  // real bug — a plain t.surface box showed up starkly against the tinted
  // highlight gradient). Safe because the gradient above is freshly repainted
  // every single render() call, immediately before this text — there's never
  // stale text underneath that needs erasing.
  gfx.setTextFont(t.fontSmall);
  gfx.setTextDatum(TC_DATUM);
  gfx.setTextColor(highlight ? t.accent : t.textPrimary);
  gfx.drawString(d.dayLabel, cx, kCardsY0 + kDayOffsetY);

  const WeatherIconId icon = getWeatherIcon(d.weatherCode, /*isDay=*/true);
  ui::drawWeatherIcon(gfx, cx, kCardsY0 + kIconOffsetY, kIconR, icon);

  char high[8];
  char low[8];
  char rain[8];
  char wind[10];
  snprintf(high, sizeof(high), "%ld%s", lroundf(convertTempC(d.highC, tempUnit)),
           tempUnitSuffix(tempUnit));
  snprintf(low, sizeof(low), "%ld%s", lroundf(convertTempC(d.lowC, tempUnit)),
           tempUnitSuffix(tempUnit));
  snprintf(rain, sizeof(rain), "%d%%", (int)d.rainProbabilityPct);
  snprintf(wind, sizeof(wind), "%ld%s", lroundf(convertWindKmh(d.windSpeedMaxKmh, windUnit)),
           windUnitSuffix(windUnit));

  gfx.setTextFont(t.fontSmall);
  gfx.setTextColor(t.textPrimary);
  gfx.drawString(high, cx, kCardsY0 + kHighOffsetY);
  gfx.setTextColor(t.textSecondary);
  gfx.drawString(low, cx, kCardsY0 + kLowOffsetY);

  gfx.setTextFont(kHeaderFont);
  gfx.drawString(rain, cx, kCardsY0 + kRainOffsetY);
  gfx.drawString(wind, cx, kCardsY0 + kWindOffsetY);
}

}  // namespace

ForecastPage::ForecastPage(DisplayManager& display, WeatherService& weather)
    : _display(display), _weather(weather) {}

void ForecastPage::tick(uint32_t nowMs) {
  (void)nowMs;
  const uint32_t version = _weather.dataVersion();
  const bool failed = _weather.lastFetchFailed();
  if (version != _lastSeenVersion || failed != _lastSeenFailed) {
    _lastSeenVersion = version;
    _lastSeenFailed = failed;
    requestRedraw();
  }
}

void ForecastPage::render(bool full) {
  // A fetch landing mid-rotation redraws via requestRedraw() -> render(false)
  // (see tick()), i.e. without a preceding fillScreen — clear the content
  // area ourselves first, same reasoning as the other live pages.
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const WeatherData d = _weather.data();
  const bool offline = _weather.lastFetchFailed();
  const AppSettings& settings = SettingsManager::instance().get();
  const auto tempUnit = static_cast<TempUnit>(settings.tempUnit);
  const auto windUnit = static_cast<WindUnit>(settings.windUnit);

  tft.fillRect(0, ui::layout::BODY_Y, cfg::display::WIDTH, ui::layout::BODY_H, t.bg);
  ui::drawPageTitle(tft, "5 Day Forecast");

  // "Updated HH:MM" (or a fetching/offline status) on the title row's right
  // side — location/date used to live in a dedicated header row below the
  // title, but both now live in the shared status bar (see
  // ui/StatusBar.cpp), so this is the only per-fetch status this page still
  // needs to show.
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

  if (!d.valid || d.dailyCount == 0) {
    const char* msg = d.valid ? "No forecast data" : (offline ? "No data (offline)" : "Fetching...");
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString(msg, cfg::display::WIDTH / 2, kCardsY0 + kCardsH / 2);
    return;
  }

  const uint8_t count = min(d.dailyCount, kCards);
  for (uint8_t i = 0; i < count; i++) {
    drawForecastCard(tft, i, d.daily[i], /*highlight=*/i == 0, tempUnit, windUnit);
  }
}

bool ForecastPage::handleTap(int16_t x, int16_t y) {
  // Only the card row is ours to consume — a tap anywhere else (the header,
  // or below the cards) falls through to PageManager's default "advance to
  // next page", same as before this page had any tap handling at all.
  if (!_onDayTapped) return false;
  if (y < kCardsY0 || y >= kCardsY0 + kCardsH) return false;

  const int32_t relX = x - ui::layout::CARD_MARGIN_X;
  if (relX < 0 || relX >= ui::layout::CARD_W) return false;
  const int32_t slot = relX / (kCardW + kGap);
  if (slot < 0 || slot >= (int32_t)kCards) return false;
  if (relX - slot * (kCardW + kGap) >= kCardW) return false;  // landed in the gap between cards

  const WeatherData d = _weather.data();
  if (!d.valid || (uint8_t)slot >= d.dailyCount) return false;

  _onDayTapped((uint8_t)slot);
  return true;
}
