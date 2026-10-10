#include "display/TouchManager.h"

#include <Preferences.h>

#include "utils/Log.h"

static const char* TAG = "TOUCH";

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

#if defined(BOARD_CYD35)
// ---------------------------------------------------------------------------
// 3.5" board: touch on the TFT's own SPI bus, read via TFT_eSPI.
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kCalNamespace = "cydtouch";  // own namespace: no AppSettings schema change
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
  LOGI(TAG, "touch calibrated and saved");
}

void TouchManager::clearCalibration() {
  Preferences prefs;
  prefs.begin(kCalNamespace, false);
  prefs.remove(kCalKey);
  prefs.end();
  LOGI(TAG, "touch calibration cleared");
}

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
bool TouchManager::begin(const AppSettings& settings) {
  applySettings(settings);
  _spi.begin(cfg::pins::TOUCH_CLK, cfg::pins::TOUCH_MISO, cfg::pins::TOUCH_MOSI,
             cfg::pins::TOUCH_CS_PIN);
  if (!_ts.begin(_spi)) {
    LOGE(TAG, "XPT2046 init failed — touch disabled, use the web portal");
    _ok = false;
    return false;
  }
  // Rotation stays fixed here — 180°/mirror handling all happens in
  // mapRawToScreen() now (see applySettings()), so this only ever needs the
  // base landscape rotation, never ROTATION_FLIPPED.
  _ts.setRotation(cfg::display::ROTATION);
  _ok = true;
  LOGI(TAG, "touch ready (cal x=%u..%u y=%u..%u)", (unsigned)_xMin, (unsigned)_xMax,
       (unsigned)_yMin, (unsigned)_yMax);
  return true;
}

void TouchManager::runCalibration() {}

void TouchManager::clearCalibration() {}

void TouchManager::reacquireBus() {
  if (!_ok) return;
  _spi.begin(cfg::pins::TOUCH_CLK, cfg::pins::TOUCH_MISO, cfg::pins::TOUCH_MOSI,
             cfg::pins::TOUCH_CS_PIN);
}

bool TouchManager::readPoint(Point& out) {
  if (!_ok) return false;
  if (!(_ts.tirqTouched() && _ts.touched())) return false;
  const TS_Point raw = _ts.getPoint();
  out = mapRawToScreen(raw.x, raw.y);
  return true;
}

void TouchManager::setProvisioningRotation(bool provisioning) {
  if (!_ok) return;
  // 3 == TouchWifiProvisioner's CYD_BasicConnect example's ts.setRotation(3)
  // — see the header comment on rawTouchProvisioning() for why this doesn't
  // just reuse cfg::display::ROTATION/ROTATION_FLIPPED.
  _ts.setRotation(provisioning ? 3 : cfg::display::ROTATION);
}

bool TouchManager::rawTouchProvisioning(Point& outPoint) {
  if (!_ok) return false;
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
