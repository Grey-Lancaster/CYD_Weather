#include "ui/AlertTheme.h"

#include "ui/Theme.h"

namespace ui {

uint16_t alertSeverityColour(AlertSeverity severity) {
  const Theme& t = theme();
  switch (severity) {
    case AlertSeverity::Danger:
      return t.error;
    case AlertSeverity::Warning:
      return t.warn;
    case AlertSeverity::Advisory:
      return t.accent;
    case AlertSeverity::Information:
    default:
      return t.textSecondary;
  }
}

void drawAlertIcon(TFT_eSPI& gfx, int32_t cx, int32_t cy, int32_t r, AlertSeverity severity) {
  const Theme& t = theme();
  const uint16_t colour = alertSeverityColour(severity);
  switch (severity) {
    case AlertSeverity::Danger:
      // Filled shape drawn immediately below the text: an opaque text
      // background matching that same fill colour is safe here (unlike
      // over a gradient/variable background — see ui/Widgets.h's
      // drawWarningBadge for the same pattern).
      gfx.fillTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, colour);
      gfx.setTextFont(1);
      gfx.setTextDatum(MC_DATUM);
      gfx.setTextColor(t.bg, colour);
      gfx.drawString("!", cx, cy + r / 3);
      break;

    case AlertSeverity::Warning:
      // Outline only — the caller's actual background varies (status bar
      // vs a card), so the "!" must use transparent text, not an opaque
      // background guess.
      gfx.drawTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, colour);
      gfx.setTextFont(1);
      gfx.setTextDatum(MC_DATUM);
      gfx.setTextColor(colour);
      gfx.drawString("!", cx, cy + r / 3);
      break;

    case AlertSeverity::Advisory:
      gfx.fillCircle(cx, cy, r, colour);
      gfx.setTextFont(1);
      gfx.setTextDatum(MC_DATUM);
      gfx.setTextColor(t.bg, colour);
      gfx.drawString("i", cx, cy);
      break;

    case AlertSeverity::Information:
    default:
      gfx.drawCircle(cx, cy, r, colour);
      gfx.fillCircle(cx, cy, max((int32_t)1, r / 4), colour);
      break;
  }
}

}  // namespace ui
