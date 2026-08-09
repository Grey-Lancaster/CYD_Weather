#include "ui/StatusBar.h"

#include <time.h>

#include "config/Version.h"
#include "storage/SettingsManager.h"
#include "ui/AlertTheme.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace {
// AppSettings::hour12 toggles AM/PM display; buffer callers size for the
// 12-hour case ("12:59 PM", 8 chars + terminator).
void formatClock(char* buf, size_t bufSize, int hour, int minute, bool hour12) {
  if (hour12) {
    const int h12 = (hour % 12 == 0) ? 12 : hour % 12;
    snprintf(buf, bufSize, "%d:%02d %s", h12, minute, hour >= 12 ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour, minute);
  }
}
}  // namespace

StatusBar::StatusBar(DisplayManager& display, NetworkManager& network, WeatherAlertManager& alerts,
                     WeatherService& weather, TimeManager& time)
    : _display(display), _network(network), _alerts(alerts), _weather(weather), _time(time) {}

void StatusBar::render(bool force) {
  const Theme& t = theme();
  const WeatherAlert* alert = _alerts.topAlert();

  // ---- location (left, preceded by a small severity glyph if an alert is
  // active) + best-effort date (centre) + time (right), with the
  // connectivity dot's colour still reflecting network state. See class
  // comment for why there's no detailed connection-status text anymore
  // (AP-mode/offline/etc.) — the dot alone carries that now.
  const uint32_t weatherVersion = _weather.dataVersion();
  if (weatherVersion != _lastSeenWeatherVersion) {
    _lastSeenWeatherVersion = weatherVersion;
    const WeatherData d = _weather.data();
    if (d.valid) {
      int h = 0, m = 0;
      if (sscanf(d.lastUpdate, "%d:%d", &h, &m) == 2) {
        _fetchMinutesOfDay = (int16_t)(h * 60 + m);
        _fetchMillis = millis();
      }
      if (d.dailyCount > 0) {
        snprintf(_cachedDate, sizeof(_cachedDate), "%s, %s", d.daily[0].dayLabel,
                 d.daily[0].dateLabel);
      }
      strlcpy(_cachedLocation, d.locationName, sizeof(_cachedLocation));
    }
  }

  char location[40];
  // Oversized vs. the realistic content ("Sat, Aug 1", ~11 chars) so
  // -Wformat-truncation (which reasons about %d's theoretical INT_MIN/MAX
  // width, not tm_mday's actual 1..31 range) stays quiet.
  char date[32];
  char clock[10];
  const bool hour12 = SettingsManager::instance().get().hour12 != 0;
  strlcpy(location, _cachedLocation[0] != '\0' ? _cachedLocation : "Location", sizeof(location));
  if (_time.isSynced()) {
    const time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    // "Xxx, Yyy" (day+month abbreviations are always exactly 3 chars) is
    // always exactly 8 characters — 9 bytes with the terminator. A smaller
    // buffer here previously made strftime() fail (returns 0, leaves the
    // buffer's contents indeterminate per the C standard) on every call,
    // which is what caused the garbled date text.
    char wdMon[16];
    strftime(wdMon, sizeof(wdMon), "%a, %b", &tmNow);  // e.g. "Sat, Aug" — same as _cachedDate below
    snprintf(date, sizeof(date), "%s %d", wdMon, tmNow.tm_mday);
    formatClock(clock, sizeof(clock), tmNow.tm_hour, tmNow.tm_min, hour12);
  } else if (_fetchMinutesOfDay >= 0) {
    const int32_t elapsedMin = (int32_t)((millis() - _fetchMillis) / 60000u);
    const int16_t nowMin = (int16_t)(((_fetchMinutesOfDay + elapsedMin) % 1440 + 1440) % 1440);
    formatClock(clock, sizeof(clock), nowMin / 60, nowMin % 60, hour12);
    strlcpy(date, _cachedDate[0] != '\0' ? _cachedDate : "--", sizeof(date));
  } else {
    strlcpy(date, "--", sizeof(date));
    strlcpy(clock, "--:--", sizeof(clock));
  }

  uint16_t dotColor = t.textSecondary;
  switch (_network.state()) {
    case NetState::Connected:
      dotColor = t.ok;
      break;
    case NetState::Connecting:
      dotColor = t.warn;
      break;
    case NetState::Disconnected:
      dotColor = t.error;
      break;
    case NetState::ApMode:
      dotColor = t.accent;
      break;
    case NetState::Idle:
    default:
      break;
  }

  // One comparable string covering everything that can change (location,
  // date, time, dot colour, alert presence/severity) — the existing
  // single-strcmp dirty-check still works unmodified, and the time ticking
  // over to a new minute is what drives this to redraw once a minute on its
  // own.
  // Oversized vs. realistic content for the same reason as `date` above —
  // location/date/clock's buffer sizes are each worst-case-sized too, so
  // -Wformat-truncation adds them up literally.
  char combined[112];
  snprintf(combined, sizeof(combined), "%s|%s|%s|%u|%d|%u", location, date, clock,
           (unsigned)dotColor, alert != nullptr ? (int)alert->severity : -1,
           alert != nullptr ? (unsigned)_alerts.version() : 0u);
  if (!force && strcmp(combined, _lastText) == 0) return;
  strlcpy(_lastText, combined, sizeof(_lastText));

  ui::renderRegion(_display, 0, t.statusBarH, [&](TFT_eSPI& gfx, int32_t x0, int32_t y0) {
    const int32_t midY = y0 + t.statusBarH / 2;
    gfx.fillRect(x0, y0, cfg::display::WIDTH, t.statusBarH, t.statusBarBg);
    gfx.drawFastHLine(x0, y0 + t.statusBarH - 1, cfg::display::WIDTH, t.border);
    gfx.setTextFont(t.fontSmall);
    gfx.setTextColor(t.statusBarText, t.statusBarBg);
    int32_t locX = x0 + t.pad;
    if (alert != nullptr) {
      // Small severity glyph in place of a full-width banner — keeps the
      // bar's normal location/date/time content visible at all times;
      // pages/PageAlerts.cpp (already in the page rotation) has the detail.
      ui::drawAlertIcon(gfx, x0 + t.pad + 6, midY, 6, alert->severity);
      locX += 16;
    }
    gfx.setTextDatum(ML_DATUM);
    gfx.drawString(location, locX, midY);
    gfx.setTextDatum(MC_DATUM);
    gfx.drawString(date, x0 + (int32_t)cfg::display::WIDTH / 2, midY);
    const int32_t dotX = x0 + (int32_t)cfg::display::WIDTH - t.pad - 3;
    gfx.fillCircle(dotX, midY, 3, dotColor);
    gfx.setTextDatum(MR_DATUM);
    gfx.drawString(clock, dotX - 10, midY);
  });
}
