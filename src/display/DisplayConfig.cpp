#include "display/DisplayConfig.h"

// Standard MIPI DCS MADCTL (0x36) command + bit positions. Identical across
// every panel driver TFT_eSPI ships, but not visible via TFT_eSPI.h (see
// DisplayConfig.h for why), so they're redefined here rather than relying on
// TFT_eSPI's internal, driver-specific macros.
namespace {
constexpr uint8_t CMD_MADCTL = 0x36;
constexpr uint8_t MADCTL_MY = 0x80;
constexpr uint8_t MADCTL_MX = 0x40;
constexpr uint8_t MADCTL_MV = 0x20;
constexpr uint8_t MADCTL_BGR = 0x08;
}  // namespace

uint8_t DisplayConfig::buildMadctl(bool flipped, ColorOrder order, bool mirrorH, bool mirrorV,
                                     bool rotate90) {
  // Landscape only: MV (rotation) is always set; MX+MY together give the
  // 180° flip (landscape, USB-left).
  uint8_t v = flipped ? (MADCTL_MX | MADCTL_MY | MADCTL_MV) : MADCTL_MV;
  // Mirroring XORs the axis bit on top of that baseline — matches the
  // MADCTL variants documented across CYD board revisions.
  if (mirrorH) v ^= MADCTL_MX;
  if (mirrorV) v ^= MADCTL_MY;
  // Board-variant wiring compensation, not a logical 90°/270° rotation of
  // the UI (see class comment) — XORs MV on top of everything above.
  if (rotate90) v ^= MADCTL_MV;
  if (order == ColorOrder::BGR) v |= MADCTL_BGR;
  return v;
}

void DisplayConfig::apply(TFT_eSPI& tft, const AppSettings& settings) {
  const uint8_t rotation = settings.displayFlipped ? cfg::display::ROTATION_FLIPPED : cfg::display::ROTATION;
  // setRotation() first: it updates TFT_eSPI's internal width/height
  // bookkeeping for the requested rotation (needed for correct clipping).
  // The MADCTL byte it writes as a side effect is then immediately
  // overwritten below with our own colour-order/mirror/rotate90 bits.
  tft.setRotation(rotation);
  tft.writecommand(CMD_MADCTL);
#if defined(BOARD_CYD35)
  // The 3.5" panel is natively landscape (no MV wiring quirk to compensate), and
  // turning MV off would make the 480x320 panel portrait, pushing the centred
  // 320x240 UI off-screen. The setting is ignored here (and hidden in the portal).
  const bool rotate90 = false;
  // Mirror display is not offered on this panel either (it reads correctly as shipped).
  const bool mirrorH = false;
  const bool mirrorV = false;
#else
  const bool rotate90 = settings.rotate90 != 0;
  const bool mirrorH = settings.mirrorH != 0;
  const bool mirrorV = settings.mirrorV != 0;
#endif
  tft.writedata(buildMadctl(settings.displayFlipped != 0, static_cast<ColorOrder>(settings.colorOrder),
                            mirrorH, mirrorV, rotate90));
  tft.invertDisplay(settings.invertColors != 0);
}
