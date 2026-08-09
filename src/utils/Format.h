#pragma once

#include <Arduino.h>

// Small, allocation-free formatting helpers.
namespace fmt {

// "3d 04:12:33" or "04:12:33"
inline void uptime(char* buf, size_t len, uint32_t ms) {
  uint32_t s = ms / 1000u;
  const uint32_t d = s / 86400u;
  s %= 86400u;
  const uint32_t h = s / 3600u;
  s %= 3600u;
  const uint32_t m = s / 60u;
  s %= 60u;
  if (d > 0) {
    snprintf(buf, len, "%lud %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
  } else {
    snprintf(buf, len, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m,
             (unsigned long)s);
  }
}

// "512 B", "123.4 KB", "4.0 MB"
inline void bytes(char* buf, size_t len, uint32_t b) {
  if (b >= 1024u * 1024u) {
    snprintf(buf, len, "%.1f MB", (double)b / (1024.0 * 1024.0));
  } else if (b >= 1024u) {
    snprintf(buf, len, "%.1f KB", (double)b / 1024.0);
  } else {
    snprintf(buf, len, "%lu B", (unsigned long)b);
  }
}

// Reformats a "HH:MM" (24-hour) string for display, respecting
// AppSettings::hour12: "14:05" -> "14:05" (hour12 false) or "2:05 PM"
// (hour12 true). The underlying stored strings this reads from
// (WeatherData::lastUpdate, HourlyForecastEntry::time) stay 24-hour always
// — several places parse/compare them directly (day-boundary detection,
// sun/moon time math), so only call this at the point of drawing, never
// change what's stored. Falls back to copying `in24` verbatim if it isn't
// a parseable "HH:MM" (e.g. the "--:--" placeholder before the first fetch).
inline void clockTime(char* buf, size_t len, const char* in24, bool hour12) {
  int hh = 0, mm = 0;
  if (sscanf(in24, "%d:%d", &hh, &mm) != 2) {
    strlcpy(buf, in24, len);
    return;
  }
  if (hour12) {
    const int h12 = (hh % 12 == 0) ? 12 : hh % 12;
    snprintf(buf, len, "%d:%02d %s", h12, mm, hh >= 12 ? "PM" : "AM");
  } else {
    snprintf(buf, len, "%02d:%02d", hh, mm);
  }
}

}  // namespace fmt
