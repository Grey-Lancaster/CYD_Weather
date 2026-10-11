#include "pages/WeatherTrendsPage.h"

#include "storage/SettingsManager.h"
#include "ui/Graph.h"
#include "ui/Widgets.h"
#include "weather/WeatherUnits.h"

namespace {

// Same "dedicated header row" pattern as every other live page — see
// ForecastPage/HourlyForecastPage/CurrentConditionsPage.
constexpr int32_t kSubHeaderY = ui::layout::BODY_Y;
constexpr int32_t kSubHeaderH = 14;
constexpr uint8_t kSubHeaderFont = 1;  // GLCD base font — compact, matches those pages

// Footer graph-selector tabs, one per Graph value, pinned just above the
// shared page-indicator dots (which PageManager draws below INDICATOR_Y —
// this page never touches that strip itself).
constexpr int32_t kFooterH = 28;
// Resistive touch is imprecise: accept taps a bit above the drawn tabs too.
constexpr int32_t kTapSlopY = 14;
constexpr int32_t kFooterY0 = ui::layout::INDICATOR_Y - kFooterH;
constexpr uint8_t kTabCount = 5;  // == (uint8_t)Graph::Count
constexpr int32_t kTabW = ui::layout::CARD_W / kTabCount;

constexpr int32_t kPlotY0 = kSubHeaderY + kSubHeaderH + 2;
constexpr int32_t kPlotY1 = kFooterY0 - 2;
constexpr int32_t kPlotH = kPlotY1 - kPlotY0;

constexpr uint32_t kAutoAdvanceMs = 8000;

using Graph = WeatherTrendsPage::Graph;

const char* graphName(Graph g) {
  switch (g) {
    case Graph::Temperature:
      return "Temperature";
    case Graph::Humidity:
      return "Humidity";
    case Graph::Pressure:
      return "Pressure";
    case Graph::WindSpeed:
      return "Wind Speed";
    case Graph::RainProbability:
      return "Rain Probability";
    default:
      return "";
  }
}

const char* graphTabLabel(Graph g) {
  switch (g) {
    case Graph::Temperature:
      return "Temp";
    case Graph::Humidity:
      return "Humid";
    case Graph::Pressure:
      return "Press";
    case Graph::WindSpeed:
      return "Wind";
    case Graph::RainProbability:
      return "Rain";
    default:
      return "";
  }
}

}  // namespace

WeatherTrendsPage::WeatherTrendsPage(DisplayManager& display) : _display(display) {}

void WeatherTrendsPage::tick(uint32_t nowMs) {
  bool dirty = false;

  // Cheap (no file I/O — see WeatherHistoryManager::getStorageUsage()); a
  // changed count means a new reading landed since the last check.
  const size_t count = WeatherHistoryManager::instance().getStorageUsage().readingCount;
  if (count != _lastSeenReadingCount) {
    _lastSeenReadingCount = count;
    dirty = true;
  }

  if (nowMs - _lastAutoAdvanceMs >= kAutoAdvanceMs) {
    _lastAutoAdvanceMs = nowMs;
    _selected = (Graph)(((uint8_t)_selected + 1) % (uint8_t)Graph::Count);
    dirty = true;
  }

  if (dirty) requestRedraw();
}

bool WeatherTrendsPage::handleTap(int16_t x, int16_t y) {
  // Only the footer tab strip is ours to consume — a tap anywhere else
  // falls through to PageManager's default "advance to next page" handling,
  // same as every other page (this page adds a control, it doesn't change
  // what an unconsumed tap already does).
  if (y < kFooterY0 - kTapSlopY || y >= ui::layout::INDICATOR_Y) return false;
  const int32_t relX = x - ui::layout::CARD_MARGIN_X;
  if (relX < 0 || relX >= ui::layout::CARD_W) return false;

  const uint8_t tab = (uint8_t)(relX / kTabW);
  if (tab >= kTabCount) return false;

  _selected = (Graph)tab;
  _lastAutoAdvanceMs = millis();  // a manual pick shouldn't be undone a moment later
  requestRedraw();
  return true;
}

void WeatherTrendsPage::render(bool full) {
  // Same reasoning as the other live pages: a data change or a tab tap
  // redraws via requestRedraw() -> render(false), i.e. without a preceding
  // fillScreen — clear the content area ourselves first.
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();

  // The title row sits above BODY_Y; clear it too or the old graph name shows through.
  tft.fillRect(0, ui::layout::CONTENT_Y, cfg::display::WIDTH,
               ui::layout::BODY_H + ui::layout::TITLE_H, t.bg);
  ui::drawPageTitle(tft, "Weather Trends");

  // Title line, right-aligned: the selected graph's name.
  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(graphName(_selected), ui::layout::CARD_MARGIN_X + ui::layout::CARD_W,
                 ui::layout::TITLE_Y);

  // Pull the newest kMaxPoints readings (oldest-first) and extract the one
  // field the selected graph needs. Temperature/wind speed are converted to
  // the user's chosen unit (WeatherHistoryReading always stores metric —
  // see storage/WeatherHistoryManager.h) so this page stays consistent with
  // every other page's unit handling.
  const size_t readingCount =
      WeatherHistoryManager::instance().getHistory(_readings, kMaxPoints);
  const AppSettings& settings = SettingsManager::instance().get();
  const auto tempUnit = static_cast<TempUnit>(settings.tempUnit);
  const auto windUnit = static_cast<WindUnit>(settings.windUnit);

  const char* unitSuffix = "";
  for (size_t i = 0; i < readingCount; i++) {
    const WeatherHistoryReading& r = _readings[i];
    switch (_selected) {
      case Graph::Temperature:
        _plotValues[i] = convertTempC(r.temperatureC, tempUnit);
        unitSuffix = tempUnitSuffix(tempUnit);
        break;
      case Graph::Humidity:
        _plotValues[i] = (float)r.humidityPct;
        unitSuffix = "%";
        break;
      case Graph::Pressure:
        _plotValues[i] = r.pressureHpa;
        unitSuffix = "hPa";
        break;
      case Graph::WindSpeed:
        _plotValues[i] = convertWindKmh(r.windSpeedKmh, windUnit);
        unitSuffix = windUnitSuffix(windUnit);
        break;
      case Graph::RainProbability:
        _plotValues[i] = (float)r.rainProbabilityPct;
        unitSuffix = "%";
        break;
      default:
        _plotValues[i] = 0.0f;
        break;
    }
  }

  const ui::GraphStats stats = ui::computeGraphStats(_plotValues, readingCount);

  // Sub-header: time range + current value (min/max are shown as the
  // graph's own axis labels instead of repeated here).
  char subHeader[40];
  if (stats.hasData) {
    snprintf(subHeader, sizeof(subHeader), "Last 24 Hours - Now %.0f%s", (double)stats.currentValue,
             unitSuffix);
  } else {
    strlcpy(subHeader, "Last 24 Hours - No data yet", sizeof(subHeader));
  }
  tft.setTextFont(kSubHeaderFont);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(subHeader, ui::layout::CARD_MARGIN_X, kSubHeaderY + kSubHeaderH / 2);

  const ui::GraphStyle style = {t.accent, t.border, t.textSecondary, t.surface, t.fontSmall};
  ui::drawLineGraph(tft, ui::layout::CARD_MARGIN_X, kPlotY0, ui::layout::CARD_W, kPlotH,
                    _plotValues, readingCount, style, unitSuffix);
  tft.drawRoundRect(ui::layout::CARD_MARGIN_X, kPlotY0, ui::layout::CARD_W, kPlotH, t.cardRadius,
                    t.border);

  // Footer: one small tab per graph, the active one accent-highlighted —
  // same rounded/bordered "card" language as the rest of the dashboard.
  for (uint8_t i = 0; i < kTabCount; i++) {
    const bool active = i == (uint8_t)_selected;
    const int32_t tx = ui::layout::CARD_MARGIN_X + i * kTabW;
    tft.fillRoundRect(tx + 1, kFooterY0, kTabW - 2, kFooterH, 4,
                      active ? t.accent : t.surface);
    tft.drawRoundRect(tx + 1, kFooterY0, kTabW - 2, kFooterH, 4, t.border);
    tft.setTextFont(kSubHeaderFont);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(active ? t.bg : t.textSecondary, active ? t.accent : t.surface);
    tft.drawString(graphTabLabel((Graph)i), tx + kTabW / 2, kFooterY0 + kFooterH / 2);
  }
}
