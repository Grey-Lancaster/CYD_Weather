#include "pages/PageAlerts.h"

#include "ui/AlertTheme.h"
#include "ui/Widgets.h"

namespace {

constexpr int32_t kHeaderY = ui::layout::BODY_Y;
constexpr int32_t kHeaderH = 14;
constexpr uint8_t kHeaderFont = 1;  // GLCD base font — matches the other pages' header rows

constexpr int32_t kListY0 = kHeaderY + kHeaderH + 4;
constexpr int32_t kCardGap = 6;
constexpr uint8_t kVisibleCards = 2;
constexpr int32_t kCardH =
    (ui::layout::INDICATOR_Y - kListY0 - (int32_t)(kVisibleCards - 1) * kCardGap) /
    (int32_t)kVisibleCards;

constexpr uint32_t kAutoAdvanceMs = 6000;

int32_t cardY(uint8_t slot) { return kListY0 + (int32_t)slot * (kCardH + kCardGap); }

const char* severityLabel(AlertSeverity s) {
  switch (s) {
    case AlertSeverity::Danger:
      return "DANGER";
    case AlertSeverity::Warning:
      return "WARNING";
    case AlertSeverity::Advisory:
      return "ADVISORY";
    case AlertSeverity::Information:
    default:
      return "INFO";
  }
}

void formatDuration(uint16_t minutes, char* buf, size_t len) {
  if (minutes == 0) {
    strlcpy(buf, "Ongoing", len);
    return;
  }
  const uint16_t hours = minutes / 60;
  const uint16_t mins = minutes % 60;
  if (hours > 0 && mins > 0) {
    snprintf(buf, len, "~%uh %um", (unsigned)hours, (unsigned)mins);
  } else if (hours > 0) {
    snprintf(buf, len, "~%uh", (unsigned)hours);
  } else {
    snprintf(buf, len, "~%um", (unsigned)mins);
  }
}

// The reason string (up to 63 chars, see WeatherAlert::reason) can be wider
// than a card's text column at this compact font — truncate defensively
// rather than let TFT_eSPI draw off the edge of the card into whatever is
// next to it.
constexpr size_t kReasonBudget = 42;
void truncateTo(const char* src, char* dst, size_t dstLen) {
  const size_t srcLen = strlen(src);
  if (srcLen < dstLen) {
    strlcpy(dst, src, dstLen);
    return;
  }
  const size_t keep = dstLen > 4 ? dstLen - 4 : 0;
  memcpy(dst, src, keep);
  dst[keep] = '\0';
  strlcat(dst, "...", dstLen);
}

void drawAlertCard(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h,
                   const WeatherAlert& a) {
  const Theme& t = theme();
  const uint16_t colour = ui::alertSeverityColour(a.severity);

  gfx.fillRoundRect(x, y, w, h, t.cardRadius, t.surface);
  gfx.drawRoundRect(x, y, w, h, t.cardRadius, t.border);
  gfx.fillRoundRect(x, y, 4, h, 2, colour);  // left accent bar in the severity colour

  ui::drawAlertIcon(gfx, x + 22, y + h / 2, 11, a.severity);

  const int32_t textX = x + 42;

  gfx.setTextFont(t.fontSmall);
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.textPrimary, t.surface);
  gfx.drawString(a.message, textX, y + 6);

  gfx.setTextFont(kHeaderFont);
  gfx.setTextDatum(TR_DATUM);
  gfx.setTextColor(colour, t.surface);
  gfx.drawString(severityLabel(a.severity), x + w - 8, y + 6);

  char reasonBuf[kReasonBudget];
  truncateTo(a.reason, reasonBuf, sizeof(reasonBuf));
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.textSecondary, t.surface);
  gfx.drawString(reasonBuf, textX, y + 28);

  char duration[16];
  formatDuration(a.expectedDurationMin, duration, sizeof(duration));
  char footer[40];
  snprintf(footer, sizeof(footer), "Issued %s - %s", a.issued, duration);
  gfx.drawString(footer, textX, y + 44);
}

}  // namespace

PageAlerts::PageAlerts(DisplayManager& display, WeatherAlertManager& alerts, WeatherService& weather)
    : _display(display), _alerts(alerts), _weather(weather) {}

void PageAlerts::tick(uint32_t nowMs) {
  bool dirty = false;
  const uint32_t version = _alerts.version();
  const bool failed = _weather.lastFetchFailed();

  if (version != _lastSeenAlertVersion || failed != _lastSeenFailed) {
    _lastSeenAlertVersion = version;
    _lastSeenFailed = failed;
    _windowStart = 0;  // fresh alert set — restart the visible window
    _lastAdvanceMs = nowMs;
    dirty = true;
  }

  const uint8_t total = _alerts.activeCount();
  const uint8_t maxStart = total > kVisibleCards ? (uint8_t)(total - kVisibleCards) : 0;
  if (maxStart > 0 && nowMs - _lastAdvanceMs >= kAutoAdvanceMs) {
    _lastAdvanceMs = nowMs;
    _windowStart = (uint8_t)((_windowStart + 1) % (maxStart + 1));
    dirty = true;
  }

  if (dirty) requestRedraw();
}

void PageAlerts::render(bool full) {
  // Same reasoning as every other live page: an alert-set change or the
  // window auto-advancing redraws via requestRedraw() -> render(false), i.e.
  // without a preceding fillScreen — clear the content area ourselves first.
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const uint8_t total = _alerts.activeCount();
  const bool offline = _weather.lastFetchFailed();

  tft.fillRect(0, ui::layout::BODY_Y, cfg::display::WIDTH, ui::layout::BODY_H, t.bg);
  ui::drawPageTitle(tft, "Weather Alerts");

  char header[24];
  if (total == 0) {
    strlcpy(header, "No active alerts", sizeof(header));
  } else {
    snprintf(header, sizeof(header), "%u active alert%s", (unsigned)total, total == 1 ? "" : "s");
  }
  tft.setTextFont(kHeaderFont);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(header, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W, ui::layout::TITLE_Y);
  if (offline) {
    ui::drawWarningBadge(tft, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W - 76,
                         ui::layout::TITLE_Y + 4);
  }

  if (total == 0) {
    const int32_t midY = kListY0 + (ui::layout::INDICATOR_Y - kListY0) / 2;
    ui::drawAlertIcon(tft, cfg::display::WIDTH / 2, midY - 16, 14, AlertSeverity::Information);
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString("No active weather alerts", cfg::display::WIDTH / 2, midY + 16);
    return;
  }

  const uint8_t visible = min((uint8_t)(total - _windowStart), kVisibleCards);
  for (uint8_t slot = 0; slot < visible; slot++) {
    drawAlertCard(tft, ui::layout::CARD_MARGIN_X, cardY(slot), ui::layout::CARD_W, kCardH,
                 _alerts.activeAt(_windowStart + slot));
  }
}
