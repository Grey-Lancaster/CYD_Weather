#include "display/TouchManager.h"

#include "utils/Log.h"

static const char* TAG = "TOUCH";

TouchManager::TouchManager()
    : _spi(VSPI), _ts(cfg::pins::TOUCH_CS_PIN, cfg::pins::TOUCH_IRQ) {}

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

void TouchManager::reacquireBus() {
  if (!_ok) return;
  _spi.begin(cfg::pins::TOUCH_CLK, cfg::pins::TOUCH_MISO, cfg::pins::TOUCH_MOSI,
             cfg::pins::TOUCH_CS_PIN);
}

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
  const bool touched = _ts.tirqTouched() && _ts.touched();
  if (touched) {
    const TS_Point raw = _ts.getPoint();
    const Point p = mapRawToScreen(raw.x, raw.y);
    if (!_wasTouched) {
      _pressPoint = p;
      _pressMs = millis();
      _pressCount++;
      _gestureConsumed = _onActivity ? _onActivity() : false;
      LOGD(TAG, "press at %d,%d (raw %d,%d)", (int)p.x, (int)p.y, (int)raw.x, (int)raw.y);
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
