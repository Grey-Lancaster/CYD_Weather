#include "pages/CurrentConditionsPage.h"

#include <math.h>

#include "storage/SettingsManager.h"
#include "ui/WeatherIconTheme.h"
#include "ui/Widgets.h"
#include "utils/Format.h"
#include "weather/WeatherIcons.h"
#include "weather/WeatherUnits.h"

namespace {

// No dedicated sub-header row any more — location and date now live in the
// shared status bar (see ui/StatusBar.cpp), and the footer below already
// carries "Updated HH:MM", so the old "location - day, date - time"
// sub-header was fully redundant. Main content starts right under the
// title (plus a small gap) and gets the reclaimed height.
constexpr int32_t kTitleGap = 6;
constexpr int32_t kFooterTextY = ui::layout::INDICATOR_Y - 6;  // BC_DATUM baseline
constexpr int32_t kMainY0 = ui::layout::BODY_Y + kTitleGap;
constexpr int32_t kMainY1 = ui::layout::INDICATOR_Y - 24;

// Left column: hero icon + huge temperature + description.
constexpr int32_t kHeroCx = ui::layout::CARD_MARGIN_X + 71;  // left column center (~142px wide)
constexpr int32_t kIconCy = kMainY0 + 28;
constexpr int32_t kIconR = 26;
constexpr int32_t kTempX = ui::layout::CARD_MARGIN_X + 20;  // left edge of the digit block
constexpr int32_t kTempCy = kMainY0 + 90;
constexpr uint8_t kTempFont = 7;  // built-in 48px "7-segment" font — digits/minus only
constexpr int32_t kDescY = kMainY0 + 120;

// Right column: 2x4 stat grid.
constexpr int32_t kGridX0 = ui::layout::CARD_MARGIN_X + 147;
constexpr int32_t kGridW = ui::layout::CARD_W - 147;
constexpr int32_t kGridColW = kGridW / 2;
constexpr int32_t kGridRowH = (kMainY1 - kMainY0) / 4;

int32_t gridColCx(uint8_t col) { return kGridX0 + col * kGridColW + kGridColW / 2; }
int32_t gridRowY(uint8_t row) { return kMainY0 + row * kGridRowH; }

// 8-point compass — plenty of precision for a small label at this size.
const char* compassLabel(int16_t deg) {
  static const char* kLabels[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int d = deg % 360;
  if (d < 0) d += 360;
  return kLabels[((d + 22) / 45) % 8];
}

void drawStat(TFT_eSPI& gfx, int32_t cx, int32_t y, const char* label, const char* value) {
  const Theme& t = theme();
  gfx.setTextDatum(TC_DATUM);
  gfx.setTextFont(t.fontSmall);
  gfx.setTextColor(t.textSecondary, t.bg);
  gfx.drawString(label, cx, y);
  gfx.setTextColor(t.textPrimary, t.bg);
  gfx.drawString(value, cx, y + 16);
}

}  // namespace

CurrentConditionsPage::CurrentConditionsPage(DisplayManager& display, WeatherService& weather)
    : _display(display), _weather(weather) {}

void CurrentConditionsPage::tick(uint32_t nowMs) {
  (void)nowMs;
  const uint32_t version = _weather.dataVersion();
  const bool failed = _weather.lastFetchFailed();
  if (version != _lastSeenVersion || failed != _lastSeenFailed) {
    _lastSeenVersion = version;
    _lastSeenFailed = failed;
    requestRedraw();
  }
}

void CurrentConditionsPage::render(bool full) {
  // A fetch landing mid-rotation redraws via requestRedraw() -> render(false)
  // (see tick()), i.e. without a preceding fillScreen — clear the content
  // area ourselves first, same reasoning as the other live pages (only ever
  // repaints on an actual data/status change, not every tick — see tick()).
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const WeatherData d = _weather.data();
  const bool offline = _weather.lastFetchFailed();
  const AppSettings& settings = SettingsManager::instance().get();
  const auto tempUnit = static_cast<TempUnit>(settings.tempUnit);
  const auto windUnit = static_cast<WindUnit>(settings.windUnit);
  const auto pressureUnit = static_cast<PressureUnit>(settings.pressureUnit);

  tft.fillRect(0, ui::layout::BODY_Y, cfg::display::WIDTH, ui::layout::BODY_H, t.bg);
  ui::drawPageTitle(tft, "Current Conditions");

  // Location and date used to repeat here (title line + a sub-header row)
  // — both now live in the shared status bar (see ui/StatusBar.cpp), and
  // the footer below already carries "Updated HH:MM", so there's nothing
  // left for a dedicated sub-header to say; the main content just starts
  // higher instead (see kMainY0).

  const WeatherIconId icon =
      d.valid ? getWeatherIcon(d.weatherCode, d.isDay) : WeatherIconId::Unknown;
  ui::drawWeatherIcon(tft, kHeroCx, kIconCy, kIconR, icon);

  char tempDigits[8];
  const char* description;
  if (d.valid) {
    snprintf(tempDigits, sizeof(tempDigits), "%ld", lroundf(convertTempC(d.temperatureC, tempUnit)));
    description = getWeatherDescription(d.weatherCode);
  } else {
    strlcpy(tempDigits, "--", sizeof(tempDigits));
    description = offline ? "No data (offline)" : "Fetching weather...";
  }

  // The hero number: the largest element on the page, readable from across
  // a room — TFT_eSPI's built-in font 7 (48px "7-segment") rather than the
  // theme's fontLarge (26px), which would look tiny by comparison here.
  // Left-anchored at a fixed x (rather than centered) so the unit suffix can
  // be placed right after the digits' measured width, regardless of whether
  // it's one, two, or three digits (e.g. a negative temperature).
  tft.setTextFont(kTempFont);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(t.textPrimary, t.bg);
  tft.drawString(tempDigits, kTempX, kTempCy);
  const int32_t digitsW = tft.textWidth(tempDigits, kTempFont);
  tft.setTextFont(t.fontLarge);
  tft.drawString(tempUnitSuffix(tempUnit), kTempX + digitsW + 4, kTempCy + 10);

  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(description, kHeroCx, kDescY);

  char feelsLike[12];
  char humidity[12];
  char pressure[16];
  char windSpeed[16];
  char windDir[8];
  char cloud[8];
  char visibility[12];
  char uvIndex[8];
  if (d.valid) {
    snprintf(feelsLike, sizeof(feelsLike), "%ld%s", lroundf(convertTempC(d.feelsLikeC, tempUnit)),
             tempUnitSuffix(tempUnit));
    snprintf(humidity, sizeof(humidity), "%d%%", (int)d.humidityPct);
    const float pressureConverted = convertPressureHpa(d.pressureHpa, pressureUnit);
    if (pressureUnit == PressureUnit::InchesOfMercury) {
      // Integer rounding would show ~30 inHg almost always (sea-level
      // pressure barely varies outside 29-31 inHg) — this is the one unit
      // where whole-number display destroys all the useful variation.
      snprintf(pressure, sizeof(pressure), "%.2f %s", (double)pressureConverted,
               pressureUnitSuffix(pressureUnit));
    } else {
      snprintf(pressure, sizeof(pressure), "%ld %s", lroundf(pressureConverted),
               pressureUnitSuffix(pressureUnit));
    }
    snprintf(windSpeed, sizeof(windSpeed), "%ld %s", lroundf(convertWindKmh(d.windSpeedKmh, windUnit)),
             windUnitSuffix(windUnit));
    strlcpy(windDir, compassLabel(d.windDirectionDeg), sizeof(windDir));
    snprintf(cloud, sizeof(cloud), "%d%%", (int)d.cloudCoverPct);
    snprintf(visibility, sizeof(visibility), "%ld km", lroundf(d.visibilityKm));
    snprintf(uvIndex, sizeof(uvIndex), "%ld", lroundf(d.uvIndex));
  } else {
    strlcpy(feelsLike, "--", sizeof(feelsLike));
    strlcpy(humidity, "--", sizeof(humidity));
    strlcpy(pressure, "--", sizeof(pressure));
    strlcpy(windSpeed, "--", sizeof(windSpeed));
    strlcpy(windDir, "--", sizeof(windDir));
    strlcpy(cloud, "--", sizeof(cloud));
    strlcpy(visibility, "--", sizeof(visibility));
    strlcpy(uvIndex, "--", sizeof(uvIndex));
  }

  drawStat(tft, gridColCx(0), gridRowY(0), "Feels Like", feelsLike);
  drawStat(tft, gridColCx(1), gridRowY(0), "Humidity", humidity);
  drawStat(tft, gridColCx(0), gridRowY(1), "Pressure", pressure);
  drawStat(tft, gridColCx(1), gridRowY(1), "Wind Speed", windSpeed);
  drawStat(tft, gridColCx(0), gridRowY(2), "Wind Dir", windDir);
  drawStat(tft, gridColCx(1), gridRowY(2), "Cloud Cover", cloud);
  drawStat(tft, gridColCx(0), gridRowY(3), "Visibility", visibility);
  drawStat(tft, gridColCx(1), gridRowY(3), "UV Index", uvIndex);

  char status[32];
  char updatedClock[10];
  fmt::clockTime(updatedClock, sizeof(updatedClock), d.valid ? d.lastUpdate : "--:--",
                settings.hour12 != 0);
  snprintf(status, sizeof(status), "Updated %s", updatedClock);
  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(offline ? t.warn : t.textSecondary, t.bg);
  tft.drawString(status, cfg::display::WIDTH / 2, kFooterTextY);
  if (offline) {
    ui::drawWarningBadge(tft, ui::layout::CARD_MARGIN_X + 8, kFooterTextY - 4);
  }
}
