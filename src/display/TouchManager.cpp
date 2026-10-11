#include "display/TouchManager.h"

#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "utils/Log.h"

static const char* TAG = "TOUCH";

namespace {
constexpr const char* kCalNamespace = "cydtouch";  // own namespace: no AppSettings schema change
}

#if !defined(BOARD_CYD35)
// The XPT2046's interrupt pin (cfg::pins::TOUCH_IRQ, GPIO36) is deliberately NOT
// handed to XPT2046_Touchscreen. With it, the library only reads the chip after
// a falling edge on that pin and never recovers if one edge is missed (noise on
// GPIO36 during heavy WiFi activity is a plausible cause of the "touch stops
// responding after a WiFi outage" report). Without it, the library samples the
// controller on every poll(): a few bytes over SPI every 20 ms, and nothing that
// can get stuck.
TouchManager::TouchManager() : _spi(VSPI), _ts(cfg::pins::TOUCH_CS_PIN) {}
#else
TouchManager::TouchManager() {}
#endif

// Which display orientation settings a calibration was made under (a calibration
// only holds for the orientation the crosshairs were drawn in).
uint8_t TouchManager::orientationSig() const {
  return (uint8_t)((_flipped ? 1 : 0) | (_mirrorH ? 2 : 0) | (_mirrorV ? 4 : 0) | (_rotate90 ? 8 : 0));
}

#if defined(BOARD_CYD35)
// ---------------------------------------------------------------------------
// 3.5" board: touch on the TFT's own SPI bus, read via TFT_eSPI.
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kCalKey = "cal";
constexpr size_t kCalBytes = 5 * sizeof(uint16_t);

bool loadCalibration(uint16_t cal[5]) {
  Preferences prefs;
  prefs.begin(kCalNamespace, true);
  const size_t n = prefs.getBytes(kCalKey, cal, kCalBytes);
  prefs.end();
  return n == kCalBytes;
}
}  // namespace

bool TouchManager::begin(const AppSettings& settings) {
  applySettings(settings);
  if (_tft == nullptr) {
    LOGE(TAG, "no display attached — touch disabled, use the web portal");
    _ok = false;
    return false;
  }
  uint16_t cal[5];
  if (loadCalibration(cal)) {
    _tft->setTouch(cal);
    _needsCal = false;
    _useAffine = true;
  } else {
    _needsCal = true;
  }
  _ok = true;
  LOGI(TAG, "touch ready on the TFT bus (%s)", _needsCal ? "needs calibration" : "calibrated");
  return true;
}

void TouchManager::runCalibration() {
  if (_tft == nullptr) return;
  _tft->fillScreen(TFT_BLACK);
  _tft->setTextColor(TFT_CYAN, TFT_BLACK);
  _tft->setTextDatum(MC_DATUM);
  _tft->drawString("Touch calibration", _tft->width() / 2, _tft->height() / 2 - 12, 4);
  _tft->setTextColor(TFT_WHITE, TFT_BLACK);
  _tft->drawString("Touch each corner as shown", _tft->width() / 2, _tft->height() / 2 + 18, 2);
  delay(2000);
  _tft->fillScreen(TFT_BLACK);
  uint16_t cal[5];
  _tft->calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 20);
  Preferences prefs;
  prefs.begin(kCalNamespace, false);
  prefs.putBytes(kCalKey, cal, kCalBytes);
  prefs.end();
  _tft->setTouch(cal);
  _needsCal = false;
  _useAffine = true;
  LOGI(TAG, "touch calibrated and saved");
}

void TouchManager::clearCalibration() {
  Preferences prefs;
  prefs.begin(kCalNamespace, false);
  prefs.remove(kCalKey);
  prefs.end();
  LOGI(TAG, "touch calibration cleared");
}

void TouchManager::requestCalibration() { clearCalibration(); }

void TouchManager::reacquireBus() {}

void TouchManager::setProvisioningRotation(bool) {}

bool TouchManager::readPoint(Point& out) {
  if (!_ok || _tft == nullptr || _needsCal) return false;
  uint16_t x = 0, y = 0;
  if (!_tft->getTouch(&x, &y, 300)) return false;
  // Panel coordinates -> UI coordinates (the UI is centered on the larger panel).
  long ux = (long)x - cfg::display::ORIGIN_X;
  long uy = (long)y - cfg::display::ORIGIN_Y;
  const long maxX = (long)cfg::display::WIDTH - 1;
  const long maxY = (long)cfg::display::HEIGHT - 1;
  if (ux < 0) ux = 0;
  if (ux > maxX) ux = maxX;
  if (uy < 0) uy = 0;
  if (uy > maxY) uy = maxY;
  out.x = (int16_t)ux;
  out.y = (int16_t)uy;
  return true;
}

bool TouchManager::rawTouchProvisioning(Point& outPoint) {
  return readPoint(outPoint);
}

#else
// ---------------------------------------------------------------------------
// 2.8" boards: dedicated XPT2046 bus, driven by XPT2046_Touchscreen.
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kAffKey = "aff";
constexpr const char* kWantKey = "want";

struct StoredAffine {
  float m[6];
  uint8_t sig;
  uint8_t pad[3];
};

#if defined(BOARD_ILI9341)
// The ILI9341 boards' touch panels differ between revisions (and from the ST7789
// board this project's original raw-range values were tuned on), so they are
// calibrated on first start.
constexpr bool kRequireCalibration = true;
#else
// The ST7789 build keeps its original calibration unless the user opts in.
constexpr bool kRequireCalibration = false;
#endif

double det3(const double m[3][3]) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
         m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// Solves the 3x3 system A*p = b by Cramer's rule.
bool solve3(const double A[3][3], const double b[3], double p[3]) {
  const double d = det3(A);
  if (fabs(d) < 1e-6) return false;
  for (int c = 0; c < 3; c++) {
    double M[3][3];
    memcpy(M, A, sizeof(M));
    for (int r = 0; r < 3; r++) M[r][c] = b[r];
    p[c] = det3(M) / d;
  }
  return true;
}

// Least-squares fit of x = a*rx + b*ry + c and y = d*rx + e*ry + f over n points
// (raw touch values -> screen pixels). Returns the RMS error in pixels.
bool fitAffine(const double* rx, const double* ry, const double* sx, const double* sy, int n,
               float out[6], double& rmsOut) {
  double A[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, (double)n}};
  double bx[3] = {0, 0, 0}, by[3] = {0, 0, 0};
  for (int i = 0; i < n; i++) {
    A[0][0] += rx[i] * rx[i];
    A[0][1] += rx[i] * ry[i];
    A[0][2] += rx[i];
    A[1][1] += ry[i] * ry[i];
    A[1][2] += ry[i];
    bx[0] += rx[i] * sx[i];
    bx[1] += ry[i] * sx[i];
    bx[2] += sx[i];
    by[0] += rx[i] * sy[i];
    by[1] += ry[i] * sy[i];
    by[2] += sy[i];
  }
  A[1][0] = A[0][1];
  A[2][0] = A[0][2];
  A[2][1] = A[1][2];
  double px[3], py[3];
  if (!solve3(A, bx, px) || !solve3(A, by, py)) return false;
  double sumSq = 0;
  for (int i = 0; i < n; i++) {
    const double ex = px[0] * rx[i] + px[1] * ry[i] + px[2] - sx[i];
    const double ey = py[0] * rx[i] + py[1] * ry[i] + py[2] - sy[i];
    sumSq += ex * ex + ey * ey;
  }
  rmsOut = sqrt(sumSq / n);
  for (int k = 0; k < 3; k++) {
    out[k] = (float)px[k];
    out[3 + k] = (float)py[k];
  }
  return true;
}
}  // namespace

bool TouchManager::begin(const AppSettings& settings) {
  applySettings(settings);
  _spi.begin(cfg::pins::TOUCH_CLK, cfg::pins::TOUCH_MISO, cfg::pins::TOUCH_MOSI,
             cfg::pins::TOUCH_CS_PIN);
  if (!_ts.begin(_spi)) {
    LOGE(TAG, "XPT2046 init failed — touch disabled, use the web portal");
    _ok = false;
    return false;
  }

  // A stored calibration is only valid for the display orientation it was made in.
  StoredAffine st;
  bool have = false;
  bool want = false;
  {
    Preferences prefs;
    prefs.begin(kCalNamespace, true);
    have = prefs.getBytes(kAffKey, &st, sizeof(st)) == sizeof(st);
    want = prefs.getBool(kWantKey, false);
    prefs.end();
  }
  if (have && st.sig == orientationSig()) {
    memcpy(_aff, st.m, sizeof(_aff));
    _useAffine = true;
  } else if (have) {
    LOGW(TAG, "stored calibration was made in a different orientation, calibrating again");
  }
  _needsCal = !_useAffine && (kRequireCalibration || want || have);

  // With a calibration the chip is read in its native axes (rotation 0) and the
  // fitted transform does the rest; without one, the original scheme applies:
  // rotation stays fixed here — 180°/mirror handling all happens in
  // mapRawToScreen() (see applySettings()), so this only ever needs the base
  // landscape rotation, never ROTATION_FLIPPED.
  _ts.setRotation(_useAffine ? 0 : cfg::display::ROTATION);
  _ok = true;
  if (_useAffine) {
    LOGI(TAG, "touch ready (calibrated)");
  } else if (_needsCal) {
    LOGI(TAG, "touch ready (needs calibration)");
  } else {
    LOGI(TAG, "touch ready (cal x=%u..%u y=%u..%u)", (unsigned)_xMin, (unsigned)_xMax,
         (unsigned)_yMin, (unsigned)_yMax);
  }
  return true;
}

void TouchManager::runCalibration() {
  if (!_ok || _tft == nullptr) return;
  _ts.setRotation(0);  // native axes; the fitted transform absorbs the orientation

  const int W = cfg::display::WIDTH;
  const int H = cfg::display::HEIGHT;
  const int inset = 40;  // away from bezels and case edges
  const int tx[5] = {inset, W - inset, W - inset, inset, W / 2};
  const int ty[5] = {inset, inset, H - inset, H - inset, H / 2};
  double rx[5], ry[5], sx[5], sy[5];
  float fit[6] = {1, 0, 0, 0, 1, 0};
  double rms = 0;
  bool fitted = false;

  _tft->fillScreen(TFT_BLACK);
  _tft->setTextDatum(MC_DATUM);
  _tft->setTextColor(TFT_CYAN, TFT_BLACK);
  _tft->drawString("Touch calibration", W / 2, H / 2 - 12, 4);
  _tft->setTextColor(TFT_WHITE, TFT_BLACK);
  _tft->drawString("Touch each crosshair as it appears", W / 2, H / 2 + 18, 2);
  delay(2000);

  for (int attempt = 0; attempt < 3; attempt++) {
    for (int i = 0; i < 5; i++) {
      _tft->fillScreen(TFT_BLACK);
      _tft->drawFastHLine(tx[i] - 14, ty[i], 29, TFT_WHITE);
      _tft->drawFastVLine(tx[i], ty[i] - 14, 29, TFT_WHITE);
      _tft->drawCircle(tx[i], ty[i], 8, TFT_GREEN);
      char label[28];
      snprintf(label, sizeof(label), "Touch the crosshair %d/5", i + 1);
      _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
      _tft->setTextDatum(MC_DATUM);
      _tft->drawString(label, W / 2, i == 4 ? H / 2 + 36 : H / 2, 2);

      while (_ts.touched()) delay(10);  // let go of the previous target first
      delay(250);
      while (!_ts.touched()) delay(10);

      // Average a short burst of samples while the finger stays down.
      double sumX = 0, sumY = 0;
      int n = 0;
      delay(60);  // skip the contact bounce
      for (int k = 0; k < 24 && _ts.touched(); k++) {
        const TS_Point p = _ts.getPoint();
        sumX += p.x;
        sumY += p.y;
        n++;
        delay(8);
      }
      if (n == 0) {  // lifted too fast: ask for this target again
        i--;
        continue;
      }
      rx[i] = sumX / n;
      ry[i] = sumY / n;
      sx[i] = tx[i];
      sy[i] = ty[i];
      LOGI(TAG, "cal point %d: screen %d,%d raw %.0f,%.0f", i + 1, tx[i], ty[i], rx[i], ry[i]);
    }
    fitted = fitAffine(rx, ry, sx, sy, 5, fit, rms);
    LOGI(TAG, "calibration fit: %s rms %.1f px", fitted ? "ok" : "failed", rms);
    if (fitted && rms <= 12.0) break;
    _tft->fillScreen(TFT_BLACK);
    _tft->setTextColor(TFT_ORANGE, TFT_BLACK);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString("Taps were not consistent", W / 2, H / 2 - 10, 4);
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("Let's try again", W / 2, H / 2 + 20, 2);
    delay(1800);
  }

  if (fitted) {
    memcpy(_aff, fit, sizeof(_aff));
    _useAffine = true;
    StoredAffine st;
    memcpy(st.m, fit, sizeof(st.m));
    st.sig = orientationSig();
    memset(st.pad, 0, sizeof(st.pad));
    Preferences prefs;
    prefs.begin(kCalNamespace, false);
    prefs.putBytes(kAffKey, &st, sizeof(st));
    prefs.remove(kWantKey);
    prefs.end();
    LOGI(TAG, "touch calibrated and saved (x: %.5f %.5f %.2f | y: %.5f %.5f %.2f)", fit[0], fit[1],
         fit[2], fit[3], fit[4], fit[5]);
  } else {
    _ts.setRotation(cfg::display::ROTATION);  // fall back to the original scheme
    LOGE(TAG, "calibration failed, using the default touch mapping");
  }
  _needsCal = false;
  _tft->fillScreen(TFT_BLACK);
}

void TouchManager::clearCalibration() {
  Preferences prefs;
  prefs.begin(kCalNamespace, false);
  prefs.remove(kAffKey);
  prefs.remove(kWantKey);
  prefs.end();
  LOGI(TAG, "touch calibration cleared");
}

void TouchManager::requestCalibration() {
  Preferences prefs;
  prefs.begin(kCalNamespace, false);
  prefs.remove(kAffKey);
  prefs.putBool(kWantKey, true);
  prefs.end();
  LOGI(TAG, "touch calibration requested for the next start");
}

void TouchManager::reacquireBus() {
  if (!_ok) return;
  // SPIClass::begin() is a no-op while the object still believes the bus is up,
  // which it does after SdCardManager borrowed and released the shared VSPI
  // peripheral (that detached this object's pins behind its back). Without the
  // end() first, the touch pins were never re-attached and touch stayed dead
  // until a reboot — triggered simply by opening the web portal, whose page
  // checks for an SD card on load.
  _spi.end();
  _spi.begin(cfg::pins::TOUCH_CLK, cfg::pins::TOUCH_MISO, cfg::pins::TOUCH_MOSI,
             cfg::pins::TOUCH_CS_PIN);
}

bool TouchManager::readPoint(Point& out) {
  if (!_ok || _needsCal) return false;
  if (!(_ts.tirqTouched() && _ts.touched())) return false;
  const TS_Point raw = _ts.getPoint();
  if (_useAffine) {
    const long maxX = (long)cfg::display::WIDTH - 1;
    const long maxY = (long)cfg::display::HEIGHT - 1;
    long x = lroundf(_aff[0] * raw.x + _aff[1] * raw.y + _aff[2]);
    long y = lroundf(_aff[3] * raw.x + _aff[4] * raw.y + _aff[5]);
    if (x < 0) x = 0;
    if (x > maxX) x = maxX;
    if (y < 0) y = 0;
    if (y > maxY) y = maxY;
    out.x = (int16_t)x;
    out.y = (int16_t)y;
    return true;
  }
  out = mapRawToScreen(raw.x, raw.y);
  return true;
}

void TouchManager::setProvisioningRotation(bool provisioning) {
  if (!_ok || _useAffine) return;  // a calibrated chip stays in its native axes
  // 3 == TouchWifiProvisioner's CYD_BasicConnect example's ts.setRotation(3)
  // — see the header comment on rawTouchProvisioning() for why this doesn't
  // just reuse cfg::display::ROTATION/ROTATION_FLIPPED.
  _ts.setRotation(provisioning ? 3 : cfg::display::ROTATION);
}

bool TouchManager::rawTouchProvisioning(Point& outPoint) {
  if (!_ok || _needsCal) return false;
  if (_useAffine) return readPoint(outPoint);
  if (!(_ts.tirqTouched() && _ts.touched())) return false;
  const TS_Point raw = _ts.getPoint();
  const long maxX = (long)cfg::display::WIDTH - 1;
  const long maxY = (long)cfg::display::HEIGHT - 1;
  long x = map(raw.x, _xMin, _xMax, 0, maxX);
  long y = map(raw.y, _yMin, _yMax, 0, maxY);
  if (x < 0) x = 0;
  if (x > maxX) x = maxX;
  if (y < 0) y = 0;
  if (y > maxY) y = maxY;
  outPoint.x = (int16_t)x;
  outPoint.y = (int16_t)y;
  return true;
}

TouchManager::Point TouchManager::mapRawToScreen(int16_t rawX, int16_t rawY) const {
  const long maxX = (long)cfg::display::WIDTH - 1;
  const long maxY = (long)cfg::display::HEIGHT - 1;
  // _rotate90 mirrors DisplayConfig's MADCTL_MV toggle (see DisplayConfig.h):
  // a board variant needing that bit also has its touch panel's row/column
  // channels swapped relative to this project's usual boards, so which raw
  // axis/calibration range maps to screen X vs. Y swaps too.
  long x = _rotate90 ? map(rawY, _yMin, _yMax, 0, maxX) : map(rawX, _xMin, _xMax, 0, maxX);
  long y = _rotate90 ? map(rawX, _xMin, _xMax, 0, maxY) : map(rawY, _yMin, _yMax, 0, maxY);
  if (x < 0) x = 0;
  if (x > maxX) x = maxX;
  if (y < 0) y = 0;
  if (y > maxY) y = maxY;
  // Flip/mirror applied last, in already-mapped screen space, following the
  // same bit algebra as DisplayConfig::buildMadctl() (each axis's MADCTL
  // mirror bit is set iff displayFlipped XOR the matching mirror setting).
  // NOTE: only exercised so far via the WiFi picker, which no longer goes
  // through this path at all (see rawTouchProvisioning() — the picker uses
  // TouchWifiProvisioner's own proven-working rotation(3)/no-mirror scheme
  // instead, since this app's calibration values turned out to be calibrated
  // against that scheme, not this one). This general path is what the main
  // app's pages use whenever displayFlipped/mirrorH/mirrorV are set via the
  // web portal — untested in combination, flag if taps drift there too.
  if (_flipped != _mirrorH) x = maxX - x;
  if (_flipped != _mirrorV) y = maxY - y;
  Point p;
  p.x = (int16_t)x;
  p.y = (int16_t)y;
  return p;
}
#endif

// ---------------------------------------------------------------------------
// Shared by both arrangements.
// ---------------------------------------------------------------------------
void TouchManager::applySettings(const AppSettings& settings) {
  _xMin = settings.touchXMin;
  _xMax = settings.touchXMax;
  _yMin = settings.touchYMin;
  _yMax = settings.touchYMax;
  // Keep the touch chip's own orientation in sync with the display's — a
  // runtime flip (display/DisplayConfig.h) would otherwise leave taps
  // mapped to the previous orientation.
  _flipped = settings.displayFlipped != 0;
  _mirrorH = settings.mirrorH != 0;
  _mirrorV = settings.mirrorV != 0;
  _rotate90 = settings.rotate90 != 0;
}

void TouchManager::poll() {
  if (!_ok) return;
  Point p;
  const bool touched = readPoint(p);
  if (touched) {
    if (!_wasTouched) {
      _pressPoint = p;
      _pressMs = millis();
      _pressCount++;
      _gestureConsumed = _onActivity ? _onActivity() : false;
      LOGD(TAG, "press at %d,%d", (int)p.x, (int)p.y);
    }
    _lastPoint = p;
  } else if (_wasTouched) {
    const uint32_t heldMs = millis() - _pressMs;
    if (!_gestureConsumed) {
      const int16_t dx = _lastPoint.x - _pressPoint.x;
      const int16_t dy = _lastPoint.y - _pressPoint.y;
      // A mostly-horizontal drag past the threshold is a swipe; anything
      // shorter and quick enough is a tap. A slow horizontal drag that never
      // clears the threshold is simply ignored (neither gesture fires).
      if (abs((int)dx) >= (int)cfg::touch::SWIPE_MIN_DX && abs((int)dx) > abs((int)dy) * 2) {
        if (_onSwipe) _onSwipe(dx < 0 ? SwipeDirection::Left : SwipeDirection::Right);
      } else if (heldMs <= cfg::touch::TAP_MAX_MS && _onTap) {
        _onTap(_pressPoint);
      }
    }
  }
  _wasTouched = touched;
}
