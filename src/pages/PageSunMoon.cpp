#include "pages/PageSunMoon.h"

#include <math.h>

#include "storage/SettingsManager.h"
#include "ui/Widgets.h"
#include "utils/Format.h"

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ============================================================================
// astro — pure calculations, deliberately free of any TFT_eSPI/gfx
// dependency (kept separate from the drawing code below).
//
// Sun times come straight from Open-Meteo (see WeatherData::sunriseTime/
// sunsetTime) — nothing to compute. The Moon has no Open-Meteo equivalent at
// all, so phase/illumination/age/rise/set are all computed here using Paul
// Schlyter's well-known low-precision formulas for lunar position
// (stjarnhimlen.se/comp/ppcomp.html, .../riset.html) — the basis of many
// small embedded "moon clock" projects. Verified against published
// moonrise/moonset times for London for 2026-08-01..03: within 2-4 minutes
// of two independent published sources once timezone was accounted for.
// Good enough for a glance at a dashboard, not for navigation — moonrise/
// moonset also use an estimated UTC offset (round(longitude/15), i.e. mean
// solar time) rather than a real timezone/DST lookup, since this device has
// no clock and no timezone database (see PageSunMoon::render()).
// ============================================================================
namespace astro {

constexpr float kD2R = 0.017453292519943295f;
constexpr float kR2D = 57.29577951308232f;
constexpr float kSynodicMonth = 29.530588861f;

float norm360(float x) {
  x = fmodf(x, 360.0f);
  if (x < 0) x += 360.0f;
  return x;
}

// Schlyter day number: 0 at 2000-01-00.0 UT (=1999-12-31 00:00 UT). `d` is
// the calendar day-of-month; `utHours` may be any real number — negative or
// >24 both correctly land on the adjacent calendar day, since this is just
// continuous day-number arithmetic. Callers exploit that to walk a *local*
// calendar day against an estimated UTC offset (see moonRiseSet()) without
// needing separate date-rollover logic.
double daysSinceY2000(int y, int m, int d, double utHours) {
  const long dnum = 367L * y - 7L * (y + (m + 9) / 12) / 4 + 275L * m / 9 + d - 730530L;
  return (double)dnum + utHours / 24.0;
}

struct MoonPosition {
  float ra;      // degrees
  float dec;     // degrees
  float distEr;  // geocentric distance, Earth radii
  float gmst0;   // degrees — Greenwich mean sidereal time at 0h UT this day
};

// Geocentric equatorial position of the Moon at `dnum` (see
// daysSinceY2000()), including Schlyter's largest perturbation terms
// (~1 arcminute accuracy for longitude/latitude).
MoonPosition moonPosition(double dnum) {
  const float ecl = 23.4393f - 3.563e-7f * (float)dnum;

  const float Ms = norm360(356.0470f + 0.9856002585f * (float)dnum);
  const float ws = norm360(282.9404f + 4.70935e-5f * (float)dnum);
  const float Ls = norm360(Ms + ws);

  const float N = norm360(125.1228f - 0.0529538083f * (float)dnum);
  constexpr float i = 5.1454f;
  const float w = norm360(318.0634f + 0.1643573223f * (float)dnum);
  constexpr float a = 60.2666f;
  constexpr float e = 0.054900f;
  const float M = norm360(115.3654f + 13.0649929509f * (float)dnum);

  float E = M + kR2D * e * sinf(M * kD2R) * (1 + e * cosf(M * kD2R));
  for (uint8_t iter = 0; iter < 6; iter++) {
    E = E - (E - kR2D * e * sinf(E * kD2R) - M) / (1 - e * cosf(E * kD2R));
  }

  const float xv = a * (cosf(E * kD2R) - e);
  const float yv = a * (sqrtf(1 - e * e) * sinf(E * kD2R));
  const float r = sqrtf(xv * xv + yv * yv);
  const float v = norm360(atan2f(yv, xv) * kR2D);

  const float vw = (v + w) * kD2R;
  const float Nr = N * kD2R;
  const float ir = i * kD2R;
  const float xeclip = r * (cosf(Nr) * cosf(vw) - sinf(Nr) * sinf(vw) * cosf(ir));
  const float yeclip = r * (sinf(Nr) * cosf(vw) + cosf(Nr) * sinf(vw) * cosf(ir));
  const float zeclip = r * (sinf(vw) * sinf(ir));

  float lon = norm360(atan2f(yeclip, xeclip) * kR2D);
  float lat = atan2f(zeclip, sqrtf(xeclip * xeclip + yeclip * yeclip)) * kR2D;
  float r2 = sqrtf(xeclip * xeclip + yeclip * yeclip + zeclip * zeclip);

  const float Lm = norm360(N + w + M);
  const float Del = norm360(Lm - Ls);
  const float F = norm360(Lm - N);

  const float lonCorr =
      -1.274f * sinf((M - 2 * Del) * kD2R) + 0.658f * sinf(2 * Del * kD2R) -
      0.186f * sinf(Ms * kD2R) - 0.059f * sinf((2 * M - 2 * Del) * kD2R) -
      0.057f * sinf((M - 2 * Del + Ms) * kD2R) + 0.053f * sinf((M + 2 * Del) * kD2R) +
      0.046f * sinf((2 * Del - Ms) * kD2R) + 0.041f * sinf((M - Ms) * kD2R) -
      0.035f * sinf(Del * kD2R) - 0.031f * sinf((M + Ms) * kD2R) -
      0.015f * sinf((2 * F - 2 * Del) * kD2R) + 0.011f * sinf((M - 4 * Del) * kD2R);
  const float latCorr = -0.173f * sinf((F - 2 * Del) * kD2R) -
                        0.055f * sinf((M - F - 2 * Del) * kD2R) -
                        0.046f * sinf((M + F - 2 * Del) * kD2R) +
                        0.033f * sinf((F + 2 * Del) * kD2R) + 0.017f * sinf((2 * M + F) * kD2R);
  const float rCorr = -0.58f * cosf((M - 2 * Del) * kD2R) - 0.46f * cosf(2 * Del * kD2R);

  lon += lonCorr;
  lat += latCorr;
  r2 += rCorr;

  const float lonr = lon * kD2R;
  const float latr = lat * kD2R;
  const float eclr = ecl * kD2R;
  const float xeq = r2 * cosf(lonr) * cosf(latr);
  const float yeq = r2 * (sinf(lonr) * cosf(latr) * cosf(eclr) - sinf(latr) * sinf(eclr));
  const float zeq = r2 * (sinf(lonr) * cosf(latr) * sinf(eclr) + sinf(latr) * cosf(eclr));

  MoonPosition out;
  out.ra = norm360(atan2f(yeq, xeq) * kR2D);
  out.dec = atan2f(zeq, sqrtf(xeq * xeq + yeq * yeq)) * kR2D;
  out.distEr = r2;
  out.gmst0 = norm360(Ls + 180.0f);
  return out;
}

// Altitude (degrees above the local horizon) and hour angle (degrees,
// negative before meridian transit/east side, positive after/west side) of
// an already-computed Moon position, for an observer at (latDeg,
// lonDegEast) at the given UT instant (`utHours` must be the same value the
// position's `dnum` was built from).
float moonAltitude(const MoonPosition& pos, float utHours, float latDeg, float lonDegEast,
                   float* outLha) {
  const float gmst = norm360(pos.gmst0 + utHours * 15.04107f);
  const float lst = norm360(gmst + lonDegEast);
  float lha = norm360(lst - pos.ra);
  if (lha > 180.0f) lha -= 360.0f;
  if (outLha != nullptr) *outLha = lha;
  const float sinAlt = clampf(sinf(latDeg * kD2R) * sinf(pos.dec * kD2R) +
                                  cosf(latDeg * kD2R) * cosf(pos.dec * kD2R) * cosf(lha * kD2R),
                              -1.0f, 1.0f);
  return asinf(sinAlt) * kR2D;
}

// The altitude threshold (degrees) at which the Moon's upper limb crosses
// the horizon, accounting for its parallax (its apparent size/altitude
// offset varies noticeably more than the Sun's, since its distance varies —
// see Schlyter's riset.html).
float moonHorizonAltitude(const MoonPosition& pos) {
  const float parallax = asinf(clampf(1.0f / pos.distEr, -1.0f, 1.0f)) * kR2D;
  return 0.7275f * parallax - 0.5667f;
}

// Moon phase/illumination — a pure function of elapsed time since a known
// new moon (2000-01-06 18:14 UT), independent of observer location.
struct MoonPhase {
  float ageDays;   // 0..~29.53, days since the preceding new moon
  float illumPct;  // 0..100
};

MoonPhase moonPhase(double dnum) {
  const double refDnum = daysSinceY2000(2000, 1, 6, 18.0 + 14.0 / 60.0);
  double age = fmod(dnum - refDnum, (double)kSynodicMonth);
  if (age < 0) age += kSynodicMonth;
  MoonPhase out;
  out.ageDays = (float)age;
  out.illumPct = (1.0f - cosf(2.0f * 3.14159265358979323846f * out.ageDays / kSynodicMonth)) * 50.0f;
  return out;
}

const char* moonPhaseName(float ageDays) {
  // 8 equal slices of the synodic month, centred on the 4 principal phases.
  const float step = kSynodicMonth / 8.0f;
  if (ageDays < step * 0.5f || ageDays >= kSynodicMonth - step * 0.5f) return "New Moon";
  if (ageDays < step * 1.5f) return "Waxing Crescent";
  if (ageDays < step * 2.5f) return "First Quarter";
  if (ageDays < step * 3.5f) return "Waxing Gibbous";
  if (ageDays < step * 4.5f) return "Full Moon";
  if (ageDays < step * 5.5f) return "Waning Gibbous";
  if (ageDays < step * 6.5f) return "Last Quarter";
  return "Waning Crescent";
}

struct RiseSet {
  bool riseValid = false;
  float riseUtHours = 0;
  bool setValid = false;
  float setUtHours = 0;
};

// Searches the *local* calendar day [y,m,d] — converted to the UT range
// [-utcOffsetHours, 24-utcOffsetHours) so the search window exactly spans
// local midnight-to-midnight — for the Moon's altitude crossing its rise/
// set threshold. Some calendar days have no moonrise, or no moonset, at all
// (the Moon rises ~50 min later each day) — riseValid/setValid are false in
// that case, not an error.
RiseSet moonRiseSet(int y, int m, int d, float latDeg, float lonDegEast, float utcOffsetHours) {
  RiseSet result;
  constexpr uint8_t kSteps = 96;  // 15-minute resolution across the day
  const float utStart = -utcOffsetHours;
  const float utEnd = 24.0f - utcOffsetHours;

  float prevAlt = 0, prevUt = 0;
  bool havePrev = false;
  for (uint8_t s = 0; s <= kSteps; s++) {
    const float ut = utStart + (utEnd - utStart) * (float)s / (float)kSteps;
    const double dnum = daysSinceY2000(y, m, d, ut);
    const MoonPosition pos = moonPosition(dnum);
    const float h0 = moonHorizonAltitude(pos);
    const float alt = moonAltitude(pos, ut, latDeg, lonDegEast, nullptr);
    if (havePrev) {
      // h0 barely changes step to step; using the current step's threshold
      // for both ends of the interval is well within this algorithm's own
      // precision budget.
      if (prevAlt < h0 && alt >= h0) {
        const float frac = (h0 - prevAlt) / (alt - prevAlt);
        result.riseValid = true;
        result.riseUtHours = prevUt + frac * (ut - prevUt);
      } else if (prevAlt >= h0 && alt < h0) {
        const float frac = (prevAlt - h0) / (prevAlt - alt);
        result.setValid = true;
        result.setUtHours = prevUt + frac * (ut - prevUt);
      }
    }
    prevAlt = alt;
    prevUt = ut;
    havePrev = true;
  }
  return result;
}

}  // namespace astro

// ============================================================================
// Page-local time helpers — combine the astro:: math above with this
// device's best-effort clock (see PageSunMoon.h). Not astronomy, just plain
// clock arithmetic, kept apart from both astro:: and the drawing code.
// ============================================================================

int16_t parseHHMM(const char* hhmm) {
  int h = 0, mnt = 0;
  if (hhmm == nullptr || sscanf(hhmm, "%d:%d", &h, &mnt) != 2) return -1;
  return (int16_t)(h * 60 + mnt);
}

void minutesToHHMM(int32_t minutes, char* buf, size_t len) {
  minutes = ((minutes % 1440) + 1440) % 1440;
  snprintf(buf, len, "%02ld:%02ld", (long)(minutes / 60), (long)(minutes % 60));
}

void formatDuration(int32_t minutes, char* buf, size_t len) {
  if (minutes < 0) minutes = 0;
  snprintf(buf, len, "%ldh %02ldm", (long)(minutes / 60), (long)(minutes % 60));
}

// ============================================================================
// Drawing
// ============================================================================

// No dedicated sub-header row — location and date now live in the shared
// status bar (see ui/StatusBar.cpp); "Updated HH:MM" moved to the title
// row's right side instead (see render()), same treatment as
// ForecastPage.cpp. The arc/bar/cards below start right under the title
// (plus a small gap) and get the reclaimed height.
constexpr int32_t kTitleGap = 6;
constexpr uint8_t kCompactFont = 1;  // GLCD base font — matches the other pages' dense rows

constexpr int32_t kArcY0 = ui::layout::BODY_Y + kTitleGap;
constexpr int32_t kArcH = 54;
constexpr int32_t kArcOuterR = kArcH - 4;
constexpr int32_t kArcInnerR = kArcOuterR - 8;

constexpr int32_t kBarY = kArcY0 + kArcH + 4;
constexpr int32_t kBarH = 10;
constexpr int32_t kBarLabelY = kBarY + kBarH + 2;

constexpr int32_t kCardsY0 = kBarLabelY + 12;
constexpr int32_t kCardsH = ui::layout::INDICATOR_Y - kCardsY0;
constexpr int32_t kCardGap = 8;
constexpr int32_t kCardW = (ui::layout::CARD_W - kCardGap) / 2;
constexpr uint8_t kCardRows = 5;

// A self-contained "sky" palette, independent of the app's Dark/Light theme
// (see ui/Theme.h) — this page's own day/night colour switch is explicitly
// part of the brief, layered only over the horizon graphic/progress bar; the
// info cards below still use the normal theme so they stay legible and
// consistent with every other page.
constexpr uint16_t kDaySkyTop = rgb565(90, 160, 235);
constexpr uint16_t kDaySkyHorizon = rgb565(195, 222, 245);
constexpr uint16_t kDayRing = rgb565(255, 205, 110);
constexpr uint16_t kSunColour = rgb565(255, 195, 60);
constexpr uint16_t kNightSkyTop = rgb565(8, 12, 32);
constexpr uint16_t kNightSkyHorizon = rgb565(28, 36, 66);
constexpr uint16_t kNightRing = rgb565(150, 160, 205);
constexpr uint16_t kMoonColour = rgb565(226, 230, 240);

uint16_t blend565(uint16_t a, uint16_t b, float t) {
  const int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int32_t r = ar + (int32_t)((br - ar) * t);
  const int32_t g = ag + (int32_t)((bg - ag) * t);
  const int32_t bl = ab + (int32_t)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// The Moon's actual phase (not just its arc position) as a proper
// crescent/gibbous disc, rendered one scanline at a time via drawFastHLine
// only — no TFT_eSPI ellipse/circle-helper primitive involved. (An earlier
// version composed fillCircle + a half-disc fill + fillEllipse; confirmed
// live, across several attempts, that fillEllipse's actual rendered size
// didn't reliably line up with a separately-rasterized half-disc boundary
// at this icon's small scale, silently erasing the whole lit crescent.
// This version owns every pixel's boundary test itself, so there's nothing
// left to disagree.)
//
// illumFrac is astro::MoonPhase::illumPct/100 (0=new, 0.5=quarter, 1=full);
// waxing picks which side is lit. Per row, the lit region is bounded by the
// disc's own edge on one side and a terminator curve on the other (or, for
// a gibbous disc, the terminator bounds a *dark* bite instead) — verified
// by hand at illumFrac=0, 0.5, 1, and 0.15 before use:
//   0    -> terminator ~= disc edge every row -> effectively nothing lit
//   0.5  -> terminator = 0 every row -> clean half-disc
//   1    -> terminator ~= disc edge every row -> effectively all lit
//   0.15 -> a clearly multi-pixel-wide crescent on the correct side
void drawMoonPhaseIcon(TFT_eSPI& gfx, int32_t cx, int32_t cy, int32_t r, uint16_t litColour,
                       uint16_t darkColour, float illumFrac, bool waxing) {
  const bool gibbous = illumFrac > 0.5f;
  // Terminator's horizontal half-width at the disc's own vertical centre:
  // r at illumFrac=0 or 1 (new/full), shrinking to 0 at illumFrac=0.5.
  const float rx = (float)r * fabsf(1.0f - 2.0f * illumFrac);

  for (int32_t dy = -r; dy <= r; dy++) {
    const float fy = (float)dy / (float)r;
    const int32_t outerI = (int32_t)(sqrtf(fmaxf(0.0f, (float)(r * r - dy * dy))) + 0.5f);
    const int32_t termI = (int32_t)(rx * sqrtf(fmaxf(0.0f, 1.0f - fy * fy)) + 0.5f);

    gfx.drawFastHLine(cx - outerI, cy + dy, 2 * outerI + 1, darkColour);

    int32_t litLo, litHi;  // inclusive column range (relative to cx) that's lit this row
    if (waxing) {
      litLo = gibbous ? -termI : termI;
      litHi = outerI;
    } else {
      litLo = -outerI;
      litHi = gibbous ? termI : -termI;
    }
    if (litLo <= litHi) {
      gfx.drawFastHLine(cx + litLo, cy + dy, litHi - litLo + 1, litColour);
    }
  }
}

// The horizon graphic: a gradient "sky" panel with a thin arc tracing the
// visible body's path from one horizon to the other, and a glowing dot at
// its current position — `bodyFrac` in [0,1] (left horizon .. right
// horizon), or < 0 if the body's position isn't known right now (no dot
// drawn, arc still shown as decoration). `moonIllumFrac`/`moonWaxing` only
// matter at night (see drawMoonPhaseIcon above) — the Sun has no comparable
// "phase" of its own, so the day branch ignores them.
void drawSkyArc(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h, bool isDay,
                float bodyFrac, float moonIllumFrac, bool moonWaxing) {
  const uint16_t skyTop = isDay ? kDaySkyTop : kNightSkyTop;
  const uint16_t skyHorizon = isDay ? kDaySkyHorizon : kNightSkyHorizon;
  const uint16_t ring = isDay ? kDayRing : kNightRing;

  // Solid horizon-colour base first so integer band-height rounding can
  // never leave a sliver of the page background showing through at the
  // bottom of the box — the gradient bands above just paint over it.
  gfx.fillRect(x, y, w, h, skyHorizon);
  constexpr uint8_t kBands = 8;
  const int32_t bandH = h / kBands;
  for (uint8_t i = 0; i < kBands; i++) {
    const float f = (float)i / (float)(kBands - 1);
    gfx.fillRect(x, y + i * bandH, w, bandH + 1, blend565(skyTop, skyHorizon, f));
  }

  const int32_t cx = x + w / 2;
  const int32_t cy = y + h;
  gfx.drawFastHLine(x, cy - 1, w, blend565(skyHorizon, TFT_WHITE, 0.15f));
  gfx.drawArc(cx, cy, kArcOuterR, kArcInnerR, 270, 90, ring, skyHorizon, true);

  if (bodyFrac >= 0.0f) {
    const float angle = (270.0f + 180.0f * clampf(bodyFrac, 0.0f, 1.0f)) * astro::kD2R;
    const float bodyR = (float)(kArcOuterR + kArcInnerR) / 2.0f;
    const int32_t bx = cx + (int32_t)(bodyR * sinf(angle));
    const int32_t by = cy - (int32_t)(bodyR * cosf(angle));
    if (isDay) {
      gfx.fillCircle(bx, by, 8, blend565(kSunColour, skyTop, 0.55f));
      gfx.fillCircle(bx, by, 5, kSunColour);
    } else {
      // No separate halo behind this one (unlike the Sun) — at this size it
      // read as a distracting ring around the phase icon rather than a
      // soft glow, confirmed live.
      drawMoonPhaseIcon(gfx, bx, by, 7, kMoonColour, skyTop, moonIllumFrac, moonWaxing);
    }
  }
}

// Daylight progress bar — a filled track from sunrise to sunset with a
// marker at the current time, plus the two end labels. `frac` < 0 means
// sunrise/sunset aren't known yet (empty track, no marker).
void drawDaylightBar(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char* startLabel, const char* endLabel, float frac) {
  const Theme& t = theme();
  gfx.fillRoundRect(x, y, w, h, h / 2, t.surface);
  gfx.drawRoundRect(x, y, w, h, h / 2, t.border);
  if (frac >= 0.0f) {
    const int32_t fillW = (int32_t)((float)w * clampf(frac, 0.0f, 1.0f));
    if (fillW > h) gfx.fillRoundRect(x, y, fillW, h, h / 2, t.warn);
    const int32_t markX = clampf((float)(x + fillW), (float)(x + h / 2), (float)(x + w - h / 2));
    gfx.fillCircle((int32_t)markX, y + h / 2, h / 2 + 2, t.accent);
    gfx.drawCircle((int32_t)markX, y + h / 2, h / 2 + 2, t.textPrimary);
  }
  gfx.setTextFont(kCompactFont);
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.textSecondary, t.bg);
  gfx.drawString(startLabel, x, y + h + 2);
  gfx.setTextDatum(TR_DATUM);
  gfx.drawString(endLabel, x + w, y + h + 2);
}

void drawStatRow(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, const char* label,
                 const char* value) {
  const Theme& t = theme();
  gfx.setTextFont(kCompactFont);
  gfx.setTextDatum(ML_DATUM);
  gfx.setTextColor(t.textSecondary, t.surface);
  gfx.drawString(label, x, y);
  gfx.setTextDatum(MR_DATUM);
  gfx.setTextColor(t.textPrimary, t.surface);
  gfx.drawString(value, x + w, y);
}

void drawStatCard(TFT_eSPI& gfx, int32_t x, int32_t y, int32_t w, int32_t h, const char* title,
                  const char* const labels[kCardRows], const char* const values[kCardRows]) {
  const Theme& t = theme();
  gfx.fillRoundRect(x, y, w, h, t.cardRadius, t.surface);
  gfx.drawRoundRect(x, y, w, h, t.cardRadius, t.border);
  gfx.setTextFont(kCompactFont);
  gfx.setTextDatum(TL_DATUM);
  gfx.setTextColor(t.accent, t.surface);
  gfx.drawString(title, x + 6, y + 4);

  const int32_t rowH = (h - 16) / kCardRows;
  for (uint8_t i = 0; i < kCardRows; i++) {
    drawStatRow(gfx, x + 6, y + 16 + rowH / 2 + i * rowH, w - 12, labels[i], values[i]);
  }
}

}  // namespace

PageSunMoon::PageSunMoon(DisplayManager& display, WeatherService& weather)
    : _display(display), _weather(weather) {}

void PageSunMoon::tick(uint32_t nowMs) {
  const uint32_t version = _weather.dataVersion();
  const bool failed = _weather.lastFetchFailed();
  bool dirty = false;

  if (version != _lastSeenVersion || failed != _lastSeenFailed) {
    _lastSeenVersion = version;
    _lastSeenFailed = failed;
    const WeatherData d = _weather.data();
    const int16_t fetchMinutes = parseHHMM(d.lastUpdate);
    if (fetchMinutes >= 0) {
      _fetchMinutesOfDay = fetchMinutes;
      _fetchMillis = nowMs;
    }
    dirty = true;
  }

  if (_fetchMinutesOfDay >= 0) {
    const int32_t elapsedMin = (int32_t)((nowMs - _fetchMillis) / 60000u);
    const int16_t minuteNow = (int16_t)(((_fetchMinutesOfDay + elapsedMin) % 1440 + 1440) % 1440);
    if (minuteNow != _lastDrawnMinute) {
      _lastDrawnMinute = minuteNow;
      dirty = true;
    }
  }

  if (dirty) requestRedraw();
}

void PageSunMoon::render(bool full) {
  // Same reasoning as every other live page: a fetch landing or the
  // once-a-minute clock tick redraws via requestRedraw() -> render(false),
  // i.e. without a preceding fillScreen — clear the content area ourselves.
  (void)full;
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const WeatherData d = _weather.data();
  const bool offline = _weather.lastFetchFailed();
  const AppSettings& settings = SettingsManager::instance().get();

  tft.fillRect(0, ui::layout::BODY_Y, cfg::display::WIDTH, ui::layout::BODY_H, t.bg);
  ui::drawPageTitle(tft, "Sun & Moon");

  // "Updated HH:MM" (or a fetching/offline status) on the title row's right
  // side — location and date used to live here and in a sub-header row
  // below, both now redundant with the shared status bar (see
  // ui/StatusBar.cpp). Same treatment as ForecastPage.cpp.
  char updated[24];
  if (d.valid) {
    char clock[10];
    fmt::clockTime(clock, sizeof(clock), d.lastUpdate, settings.hour12 != 0);
    snprintf(updated, sizeof(updated), "Updated %s", clock);
  } else {
    strlcpy(updated, offline ? "No data (offline)" : "Fetching...", sizeof(updated));
  }
  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(t.textSecondary, t.bg);
  tft.drawString(updated, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W, ui::layout::TITLE_Y);
  if (offline) {
    const int32_t updatedW = tft.textWidth(updated, t.fontSmall);
    ui::drawWarningBadge(tft, ui::layout::CARD_MARGIN_X + ui::layout::CARD_W - updatedW - 14,
                         ui::layout::TITLE_Y + 7);
  }

  if (!d.valid) {
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString(offline ? "No data (offline)" : "Fetching...", cfg::display::WIDTH / 2,
                  ui::layout::BODY_Y + ui::layout::BODY_H / 2);
    return;
  }

  // "Now", best-effort (see class comment in PageSunMoon.h). Falls back to
  // the fetch's own timestamp if this page just became active and hasn't
  // ticked yet (tick() only runs for the active page — see PageManager).
  int16_t nowMin = _lastDrawnMinute;
  if (nowMin < 0) nowMin = parseHHMM(d.lastUpdate);

  const int16_t sunriseMin = parseHHMM(d.sunriseTime);
  const int16_t sunsetMin = parseHHMM(d.sunsetTime);
  const bool haveSun = sunriseMin >= 0 && sunsetMin > sunriseMin;
  const bool isDay = haveSun && nowMin >= 0 ? (nowMin >= sunriseMin && nowMin < sunsetMin) : d.isDay;

  // ---- Moon: today's date, an estimated UTC offset (this device has no
  // timezone database — see astro:: namespace comment), phase/illumination
  // for "now", rise/set for today, and (if currently above the horizon) a
  // live altitude/hour-angle for the horizon-graphic dot.
  int isoY = 0, isoM = 0, isoD = 0;
  sscanf(d.isoTime, "%d-%d-%d", &isoY, &isoM, &isoD);
  const float utcOffsetHours = roundf(settings.longitude / 15.0f);
  const float utNow = (nowMin >= 0 ? (float)nowMin / 60.0f : 12.0f) - utcOffsetHours;
  const double dnumNow = astro::daysSinceY2000(isoY, isoM, isoD, utNow);
  const astro::MoonPhase phase = astro::moonPhase(dnumNow);
  const astro::RiseSet moonRS =
      astro::moonRiseSet(isoY, isoM, isoD, settings.latitude, settings.longitude, utcOffsetHours);

  bool moonVisible = false;
  float moonDotFrac = 0.5f;
  if (nowMin >= 0) {
    const astro::MoonPosition posNow = astro::moonPosition(dnumNow);
    float lhaNow = 0;
    const float altNow =
        astro::moonAltitude(posNow, utNow, settings.latitude, settings.longitude, &lhaNow);
    if (altNow >= astro::moonHorizonAltitude(posNow)) {
      moonVisible = true;
      moonDotFrac = clampf(0.5f + (lhaNow / 180.0f) * 0.5f, 0.05f, 0.95f);
    }
  }

  // ---- Horizon graphic + daylight progress bar.
  float bodyFrac = -1.0f;
  if (isDay && haveSun && nowMin >= 0) {
    bodyFrac = (float)(nowMin - sunriseMin) / (float)(sunsetMin - sunriseMin);
  } else if (!isDay && moonVisible) {
    bodyFrac = moonDotFrac;
  }
  const bool moonWaxing = phase.ageDays < (astro::kSynodicMonth / 2.0f);
  drawSkyArc(tft, ui::layout::CARD_MARGIN_X, kArcY0, ui::layout::CARD_W, kArcH, isDay, bodyFrac,
            phase.illumPct / 100.0f, moonWaxing);

  const float dayFrac =
      haveSun && nowMin >= 0 ? (float)(nowMin - sunriseMin) / (float)(sunsetMin - sunriseMin) : -1.0f;
  drawDaylightBar(tft, ui::layout::CARD_MARGIN_X, kBarY, ui::layout::CARD_W, kBarH,
                  haveSun ? d.sunriseTime : "--:--", haveSun ? d.sunsetTime : "--:--", dayFrac);

  // ---- Sun stat card.
  char solarNoon[6] = "--:--";
  char dayLength[10] = "--";
  char remaining[10] = "--";
  if (haveSun) {
    minutesToHHMM((sunriseMin + sunsetMin) / 2, solarNoon, sizeof(solarNoon));
    formatDuration(sunsetMin - sunriseMin, dayLength, sizeof(dayLength));
    if (isDay && nowMin >= 0) {
      formatDuration(sunsetMin - nowMin, remaining, sizeof(remaining));
    } else {
      strlcpy(remaining, "-", sizeof(remaining));
    }
  }
  const char* sunLabels[kCardRows] = {"Sunrise", "Solar Noon", "Sunset", "Day Length", "Remaining"};
  const char* sunValues[kCardRows] = {haveSun ? d.sunriseTime : "--:--", solarNoon,
                                      haveSun ? d.sunsetTime : "--:--", dayLength, remaining};
  drawStatCard(tft, ui::layout::CARD_MARGIN_X, kCardsY0, kCardW, kCardsH, "Sun", sunLabels,
              sunValues);

  // ---- Moon stat card.
  char illum[8];
  char age[12];
  char moonrise[6] = "--:--";
  char moonset[6] = "--:--";
  snprintf(illum, sizeof(illum), "%ld%%", lroundf(phase.illumPct));
  snprintf(age, sizeof(age), "%.1f days", (double)phase.ageDays);
  if (moonRS.riseValid) {
    minutesToHHMM((int32_t)lroundf((moonRS.riseUtHours + utcOffsetHours) * 60.0f), moonrise,
                  sizeof(moonrise));
  }
  if (moonRS.setValid) {
    minutesToHHMM((int32_t)lroundf((moonRS.setUtHours + utcOffsetHours) * 60.0f), moonset,
                  sizeof(moonset));
  }
  const char* moonLabels[kCardRows] = {"Phase", "Illumination", "Moonrise", "Moonset", "Age"};
  const char* moonValues[kCardRows] = {astro::moonPhaseName(phase.ageDays), illum, moonrise,
                                       moonset, age};
  drawStatCard(tft, ui::layout::CARD_MARGIN_X + kCardW + kCardGap, kCardsY0, kCardW, kCardsH,
              "Moon", moonLabels, moonValues);
}
