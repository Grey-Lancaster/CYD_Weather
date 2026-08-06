#pragma once

#include <TFT_eSPI.h>

#include "storage/SettingsManager.h"

// Colour order the panel expects on its data bus. Different
// ESP32-2432S028R ("CYD") board revisions ship with either wiring; this lets
// it be corrected at runtime instead of a recompile. Persisted as
// AppSettings::colorOrder (raw uint8_t) — see cfg::display::COLOR_ORDER_COUNT
// for the matching bound and cfg::defaults::COLOR_ORDER for the default.
enum class ColorOrder : uint8_t { RGB = 0, BGR = 1 };

// Applies the panel-orientation settings that vary between CYD board
// revisions — landscape flip, colour order, horizontal/vertical mirror, a
// row/column swap and colour inversion — directly via the MADCTL register
// and the display-invert command. Isolated from DisplayManager (which owns
// the backlight and the shared canvas) so these hardware-compatibility
// knobs have one place to live, and so every page automatically inherits
// them simply by drawing through the one TFT_eSPI instance DisplayManager
// owns.
//
// Only the two landscape orientations exist here — this UI's layout
// (ui/Widgets.h) is fixed for a 320x240 landscape frame, so portrait is not
// a supported page-manager state and there is no MADCTL case for it.
// AppSettings::rotate90 is not a real 4-way rotation either: it's a wiring
// compensation bit (see buildMadctl() below) for board variants whose MV
// polarity is reversed relative to this project's usual boards, which
// otherwise makes the fixed 320x240 landscape frame come out portrait and
// cropped — enabling it corrects that back to landscape, it doesn't turn a
// correctly-landscape panel into portrait.
//
// MADCTL (register 0x36) bit layout — standard MIPI DCS, shared by every
// panel driver TFT_eSPI ships (ILI9341, ST7789, HX8357, ...); not exposed by
// TFT_eSPI.h itself (the driver-specific defines are private to its .cpp),
// so the bit positions are redefined in DisplayConfig.cpp:
//   MY (row order) · MX (column order) · MV (row/column swap, i.e. rotation)
//   BGR (colour order) — mirroring XORs MX (horizontal) / MY (vertical), and
//   rotate90 XORs MV, each independently on top of whatever the flip already
//   set.
//
// Note: TouchManager's raw-to-screen mapping re-reads
// AppSettings::displayFlipped/mirrorH/mirrorV/rotate90 on every
// applySettings() call so touch stays aligned with the display.
class DisplayConfig {
 public:
  // Must run after tft.init(). Safe to call again any time settings change —
  // it fully re-derives orientation/colour-order/mirror/rotate/invert from
  // `settings`.
  static void apply(TFT_eSPI& tft, const AppSettings& settings);

 private:
  static uint8_t buildMadctl(bool flipped, ColorOrder order, bool mirrorH, bool mirrorV,
                              bool rotate90);
};
