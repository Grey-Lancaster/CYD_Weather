#include "ui/Theme.h"

static const Theme kDarkTheme = {
    rgb565(16, 20, 28),     // bg
    rgb565(28, 35, 46),     // surface
    rgb565(44, 54, 68),     // border
    rgb565(232, 234, 237),  // textPrimary
    rgb565(150, 170, 190),  // textSecondary
    rgb565(90, 160, 255),   // accent
    rgb565(80, 200, 120),   // ok
    rgb565(240, 180, 60),   // warn
    rgb565(230, 90, 80),    // error
    rgb565(22, 28, 38),     // statusBarBg
    rgb565(200, 210, 220),  // statusBarText
    2,                      // fontSmall
    4,                      // fontLarge
    10,                     // pad
    6,                      // cardRadius
    cfg::ui::STATUS_BAR_H,  // statusBarH
};

static const Theme kLightTheme = {
    rgb565(244, 246, 249),  // bg
    rgb565(255, 255, 255),  // surface
    rgb565(210, 216, 224),  // border
    rgb565(28, 32, 38),     // textPrimary
    rgb565(100, 112, 126),  // textSecondary
    rgb565(30, 100, 220),   // accent
    rgb565(30, 150, 80),    // ok
    rgb565(190, 130, 10),   // warn
    rgb565(200, 55, 45),    // error
    rgb565(230, 234, 239),  // statusBarBg
    rgb565(40, 46, 54),     // statusBarText
    2,                      // fontSmall
    4,                      // fontLarge
    10,                     // pad
    6,                      // cardRadius
    cfg::ui::STATUS_BAR_H,  // statusBarH
};

// Five-colour palettes {bg, surface, border, textPrimary, textSecondary}
// borrowed from a sibling dashboard project as a starting point (see the
// caller's request) and stretched to this project's richer per-role Theme
// struct:
//   - accent reuses the palette's 5th (textSecondary) colour, not its 3rd
//     (border) — border is one of the two dark background tones in these
//     palettes, so using it for accent too made highlighted/emphasised text
//     (e.g. ForecastPage's "today" card label, drawn in t.accent) nearly
//     invisible against its own similarly-dark card background. The 5th
//     colour is the palette's brightest non-text tone, matching how the
//     Dark/Light themes' own accent is meant to read clearly against a dark
//     surface.
//   - ok/warn/error stay Dark theme's tuned-for-a-dark-background values —
//     they're semantic (good/warning/danger), not decorative, so they stay
//     consistent no matter which of these dark palettes is active.
//   - statusBarBg reuses surface, statusBarText reuses textPrimary; layout
//     metrics are the same fixed values every theme uses (see the Theme
//     struct's own comment).
namespace {
constexpr uint16_t kOk = rgb565(80, 200, 120);
constexpr uint16_t kWarn = rgb565(240, 180, 60);
constexpr uint16_t kError = rgb565(230, 90, 80);
}  // namespace

static const Theme kNavyTheme = {
    rgb565hex(0x07111F),    // bg
    rgb565hex(0x0E2034),    // surface
    rgb565hex(0x15304B),    // border
    rgb565hex(0xF4F7FB),    // textPrimary
    rgb565hex(0x7794AC),    // textSecondary
    rgb565hex(0x7794AC),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x0E2034),    // statusBarBg
    rgb565hex(0xF4F7FB),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kMidnightBlueTheme = {
    rgb565hex(0x000000),    // bg
    rgb565hex(0x000030),    // surface
    rgb565hex(0x000068),    // border
    rgb565hex(0xE6ECF7),    // textPrimary
    rgb565hex(0x6078AD),    // textSecondary
    rgb565hex(0x6078AD),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x000030),    // statusBarBg
    rgb565hex(0xE6ECF7),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kSlateGreenTheme = {
    rgb565hex(0x0C0F05),    // bg
    rgb565hex(0x181E0A),    // surface
    rgb565hex(0x2E3A17),    // border
    rgb565hex(0xF0F7EA),    // textPrimary
    rgb565hex(0x95AE7F),    // textSecondary
    rgb565hex(0x95AE7F),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x181E0A),    // statusBarBg
    rgb565hex(0xF0F7EA),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kVioletDuskTheme = {
    rgb565hex(0x0D0714),    // bg
    rgb565hex(0x1A0E2A),    // surface
    rgb565hex(0x3A2050),    // border
    rgb565hex(0xF3ECFA),    // textPrimary
    rgb565hex(0xA48FC0),    // textSecondary
    rgb565hex(0xA48FC0),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x1A0E2A),    // statusBarBg
    rgb565hex(0xF3ECFA),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kMahoganyTheme = {
    rgb565hex(0x180808),    // bg
    rgb565hex(0x40140E),    // surface
    rgb565hex(0x602018),    // border
    rgb565hex(0xFAF2F0),    // textPrimary
    rgb565hex(0xC09088),    // textSecondary
    rgb565hex(0xC09088),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x40140E),    // statusBarBg
    rgb565hex(0xFAF2F0),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kCarbonRedTheme = {
    rgb565hex(0x050505),    // bg
    rgb565hex(0x180808),    // surface
    rgb565hex(0x350F12),    // border
    rgb565hex(0xF8EEEE),    // textPrimary
    rgb565hex(0xD06060),    // textSecondary
    rgb565hex(0xD06060),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x180808),    // statusBarBg
    rgb565hex(0xF8EEEE),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kBlackRedTheme = {
    rgb565hex(0x000000),    // bg
    rgb565hex(0x180000),    // surface
    rgb565hex(0x500000),    // border
    rgb565hex(0xFFFFFF),    // textPrimary
    rgb565hex(0xFF3030),    // textSecondary
    rgb565hex(0xFF3030),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x180000),    // statusBarBg
    rgb565hex(0xFFFFFF),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static const Theme kMauveTheme = {
    rgb565hex(0x14070D),    // bg
    rgb565hex(0x2A0E1A),    // surface
    rgb565hex(0x50203A),    // border
    rgb565hex(0xFAECF3),    // textPrimary
    rgb565hex(0xC08FA4),    // textSecondary
    rgb565hex(0xC08FA4),    // accent
    kOk, kWarn, kError,
    rgb565hex(0x2A0E1A),    // statusBarBg
    rgb565hex(0xFAECF3),    // statusBarText
    2, 4, 10, 6, cfg::ui::STATUS_BAR_H,
};

static AppTheme g_activeTheme = AppTheme::Dark;

void setActiveTheme(uint8_t themeId) {
  g_activeTheme = themeId >= THEME_COUNT ? AppTheme::Dark : static_cast<AppTheme>(themeId);
}

AppTheme activeTheme() { return g_activeTheme; }

const Theme& theme() {
  switch (g_activeTheme) {
    case AppTheme::Light:
      return kLightTheme;
    case AppTheme::Navy:
      return kNavyTheme;
    case AppTheme::MidnightBlue:
      return kMidnightBlueTheme;
    case AppTheme::SlateGreen:
      return kSlateGreenTheme;
    case AppTheme::VioletDusk:
      return kVioletDuskTheme;
    case AppTheme::Mahogany:
      return kMahoganyTheme;
    case AppTheme::CarbonRed:
      return kCarbonRedTheme;
    case AppTheme::BlackRed:
      return kBlackRedTheme;
    case AppTheme::Mauve:
      return kMauveTheme;
    case AppTheme::Dark:
    default:
      return kDarkTheme;
  }
}
