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
  _ts.setRotation(_flipped ? cfg::display::ROTATION_FLIPPED : cfg::display::ROTATION);
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
  if (_ok) _ts.setRotation(_flipped ? cfg::display::ROTATION_FLIPPED : cfg::display::ROTATION);
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

bool TouchManager::rawTouch(Point& outPoint) {
  if (!_ok) return false;
  if (!(_ts.tirqTouched() && _ts.touched())) return false;
  const TS_Point raw = _ts.getPoint();
  outPoint = mapRawToScreen(raw.x, raw.y);
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
  // Mirroring is applied last, in already-mapped screen space — independent
  // of calibration and of the swap above.
  if (_mirrorH) x = maxX - x;
  if (_mirrorV) y = maxY - y;
  Point p;
  p.x = (int16_t)x;
  p.y = (int16_t)y;
  return p;
}
