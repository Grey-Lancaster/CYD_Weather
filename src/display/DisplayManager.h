#pragma once

#include <TFT_eSPI.h>

#include "config/AppConfig.h"
#include "display/DisplayConfig.h"
#include "storage/SettingsManager.h"

// Owns the ILI9341 panel, the backlight (LEDC PWM with auto-dim) and one
// shared off-screen sprite ("canvas") used for flicker-free region rendering.
//
// Canvas contract: acquireCanvas() returns the shared sprite sized w x h
// (reallocating only when the size changes), or nullptr if the allocation
// failed / exceeds cfg::display::CANVAS_MAX_BYTES — callers must then fall
// back to drawing directly on tft() (see ui::renderRegion).
class DisplayManager {
 public:
  DisplayManager();

  bool begin(const AppSettings& settings);
  void applySettings(const AppSettings& settings);

  TFT_eSPI& tft() { return _tft; }
  // Boards whose panel is larger than the 320x240 UI (cfg::display::ORIGIN_X/Y):
  // the UI is drawn through a TFT_eSPI viewport centered on the panel. No-ops on
  // 320x240 panels. Call applyViewport() after anything that calls setRotation()
  // (which resets the viewport) and resetViewport() before drawing in absolute
  // panel coordinates (touch calibration, the WiFi picker's raw flush).
  void applyViewport();
  void resetViewport();
  TFT_eSprite* acquireCanvas(uint16_t w, uint16_t h);
  void pushCanvas(int32_t x, int32_t y);

  void setBrightness(uint8_t level);
  // Feed user activity (touch). Returns true if this woke a dimmed display —
  // callers should then consume the triggering gesture.
  bool noteActivity();
  bool isDimmed() const { return _dimmed; }
  // Drives the backlight PWM directly, bypassing the stored brightness level
  // and auto-dim bookkeeping — used by PageManager to fade across a page
  // switch without disturbing what noteActivity()/tick() restore to.
  void setBacklightRaw(uint8_t duty) { writeBacklight(duty); }
  // The level the backlight should be resting at right now (accounts for
  // auto-dim) — what a fade transition should animate to/from.
  uint8_t restingBrightness() const { return _dimmed ? _dimBrightness : _brightness; }
  // Periodic auto-dim check; call from the UI tick task.
  void tick();

 private:
  void writeBacklight(uint8_t duty);

  TFT_eSPI _tft;
  TFT_eSprite _canvas;
  uint16_t _canvasW = 0;
  uint16_t _canvasH = 0;
  bool _canvasOk = false;
  uint8_t _brightness = 0;
  uint8_t _dimBrightness = 0;
  uint16_t _dimTimeoutS = 0;
  uint32_t _lastActivityMs = 0;
  bool _dimmed = false;
};
