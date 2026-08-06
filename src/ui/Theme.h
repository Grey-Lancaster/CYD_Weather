#pragma once

#include <TFT_eSPI.h>

#include "config/AppConfig.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Convenience for palettes authored as 24-bit 0xRRGGBB (e.g. Theme.cpp's
// Navy/Midnight Blue/... palettes, sourced from a sibling project) instead
// of separate r,g,b arguments.
constexpr uint16_t rgb565hex(uint32_t rgb) {
  return rgb565((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

// Persisted as AppSettings::theme (raw uint8_t) — see cfg::theme for the
// matching default/count.
enum class AppTheme : uint8_t {
  Dark = 0,
  Light = 1,
  Navy = 2,
  MidnightBlue = 3,
  SlateGreen = 4,
  VioletDusk = 5,
  Mahogany = 6,
  CarbonRed = 7,
  BlackRed = 8,
  Mauve = 9,
};
constexpr uint8_t THEME_COUNT = 10;

// Single source of visual truth for the on-device UI. One instance per
// AppTheme; layout metrics (font/pad/radius/bar height) are intentionally
// identical across themes — only color changes with theme.
struct Theme {
  uint16_t bg;
  uint16_t surface;
  uint16_t border;
  uint16_t textPrimary;
  uint16_t textSecondary;
  uint16_t accent;
  uint16_t ok;
  uint16_t warn;
  uint16_t error;
  uint16_t statusBarBg;
  uint16_t statusBarText;
  uint8_t fontSmall;   // TFT_eSPI built-in font number (16 px)
  uint8_t fontLarge;   // TFT_eSPI built-in font number (26 px)
  uint8_t pad;
  uint8_t cardRadius;
  uint8_t statusBarH;
};

// Selects the active theme (clamped to a valid AppTheme). Call again whenever
// AppSettings::theme changes; callers should also force a full page redraw
// since existing dirty-flag regions won't repaint on their own.
void setActiveTheme(uint8_t themeId);
AppTheme activeTheme();

// Returns the currently active theme's palette.
const Theme& theme();
