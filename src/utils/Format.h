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

}  // namespace fmt
