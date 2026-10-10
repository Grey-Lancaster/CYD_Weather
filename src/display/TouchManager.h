#pragma once

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include <functional>

#include "config/AppConfig.h"
#include "storage/SettingsManager.h"

// XPT2046 resistive touch. Polled by the scheduler; emits tap events via
// callback. Two hardware arrangements, chosen at build time:
//
//  * 2.8" boards (default): the controller sits on its own SPI bus (VSPI — the
//    TFT owns HSPI), driven by XPT2046_Touchscreen. Raw-to-screen mapping uses
//    the persisted calibration values.
//  * 3.5" board (BOARD_CYD35): the controller shares the TFT's SPI pins, so it
//    is read through TFT_eSPI itself (a second driver would reconfigure the
//    TFT's pins and freeze the display). It needs a one-time four-corner
//    calibration (needsCalibration()/runCalibration()), stored in its own NVS
//    namespace. The UI is shown centered on the larger panel, so touch points
//    are shifted by cfg::display::ORIGIN_X/Y into the UI's 320x240 space.
class TouchManager {
 public:
  struct Point {
    int16_t x = 0;
    int16_t y = 0;
  };
  enum class SwipeDirection : uint8_t { Left, Right };
  using TapCallback = std::function<void(Point)>;
  using SwipeCallback = std::function<void(SwipeDirection)>;
  // Called at gesture start; return true to consume the gesture (e.g. a touch
  // that only wakes a dimmed display should not also navigate).
  using ActivityCallback = std::function<bool()>;

  TouchManager();

  // The display the touch controller shares a bus with (3.5" board only; unused
  // elsewhere). Call before begin().
  void attachDisplay(TFT_eSPI& tft) { _tft = &tft; }

  bool begin(const AppSettings& settings);
  void applySettings(const AppSettings& settings);
  void poll();

  // 3.5" board: true until a calibration has been stored. Always false on the
  // 2.8" boards, which use the persisted raw-range values instead.
  bool needsCalibration() const { return _needsCal; }
  // Interactive four-corner calibration (blocks until all corners are touched).
  // Draw in absolute panel coordinates: call DisplayManager::resetViewport()
  // first and applyViewport() afterwards. No-op on the 2.8" boards.
  void runCalibration();
  // Forget the stored calibration (the next start asks again). No-op on 2.8".
  void clearCalibration();

  // Re-applies this bus's pin mapping to the shared VSPI peripheral. Needed
  // after anything else borrows VSPI with a different pin mapping — namely
  // storage/SdCardManager, which shares the hardware peripheral (there are
  // only two on this chip, and the TFT already owns the other one) via its
  // own SPIClass(VSPI) instance on the card's true default pins. A no-op if
  // touch never initialized successfully, and on the 3.5" board (touch is on
  // the TFT's bus there).
  void reacquireBus();

  void onTap(TapCallback cb) { _onTap = cb; }
  void onSwipe(SwipeCallback cb) { _onSwipe = cb; }
  void onActivity(ActivityCallback cb) { _onActivity = cb; }
  bool isTouched() const { return _wasTouched; }

  // Diagnostic-only (added to help root-cause a rare, hard-to-reproduce bug
  // where the XPT2046 stops reporting touches after a WiFi outage — see
  // main.cpp's "diag.heartbeat" task). Counts presses seen by poll() since
  // boot; 0 presses over a long uptime, cross-referenced against
  // NetworkManager's outage log, is the signal that'd confirm or rule out
  // the WiFi-outage correlation next time this happens.
  uint32_t pressCount() const { return _pressCount; }
  bool touchOk() const { return _ok; }

  // For LVGL's touch-indev read callback (see network/WifiProvisioning.cpp)
  // during the pre-connect provisioning phase, before the "touch.poll"
  // scheduler task (which drives poll() instead) exists — so there's no
  // double-read of the touch controller between the two. Deliberately
  // bypasses this app's own flip/mirror settings and mapRawToScreen():
  // TouchWifiProvisioner's own tested-working example (CYD_BasicConnect)
  // uses a plain, un-mirrored `ts.setRotation(3)` + calibration map(), and
  // this project's stored touch calibration values turn out to be exactly
  // that example's — calibrated against that scheme, not this app's MADCTL-
  // driven one. setProvisioningRotation(true) switches the XPT2046's own
  // rotation register to match before calling this; the app restores it via
  // setProvisioningRotation(false) once connected (see main.cpp's
  // startApp()). On the 3.5" board both are simply the calibrated TFT_eSPI
  // reading (no separate rotation register).
  bool rawTouchProvisioning(Point& outPoint);
  void setProvisioningRotation(bool provisioning);

 private:
#if !defined(BOARD_CYD35)
  Point mapRawToScreen(int16_t rawX, int16_t rawY) const;
#endif
  // One touch reading in UI coordinates; false when not touched.
  bool readPoint(Point& out);

  TFT_eSPI* _tft = nullptr;
#if !defined(BOARD_CYD35)
  SPIClass _spi;
  XPT2046_Touchscreen _ts;
#endif
  TapCallback _onTap;
  SwipeCallback _onSwipe;
  ActivityCallback _onActivity;
  uint16_t _xMin = 0, _xMax = 0, _yMin = 0, _yMax = 0;
  bool _flipped = false;  // mirrors AppSettings::displayFlipped
  // Mirror this project's own set of orientation-compatibility knobs
  // (display/DisplayConfig.h applies the display-side equivalents) so taps
  // stay aligned with whatever the panel is actually showing.
  bool _mirrorH = false;
  bool _mirrorV = false;
  bool _rotate90 = false;
  Point _pressPoint;
  Point _lastPoint;  // most recent point while touched, tracked for swipe distance
  uint32_t _pressMs = 0;
  uint32_t _pressCount = 0;  // diagnostic-only, see pressCount()
  bool _wasTouched = false;
  bool _gestureConsumed = false;
  bool _ok = false;
  bool _needsCal = false;
};
