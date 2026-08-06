#include "display/DisplayManager.h"

#include "utils/Log.h"

static const char* TAG = "DISP";

DisplayManager::DisplayManager() : _canvas(&_tft) {}

bool DisplayManager::begin(const AppSettings& settings) {
  _tft.init();
  // Orientation/colour-order/mirror/invert first, so the fillScreen() below
  // (and everything painted after it) lands on the final, correct panel
  // configuration — avoids a visible flicker from re-applying MADCTL after
  // content is already on screen.
  DisplayConfig::apply(_tft, settings);
  _tft.fillScreen(TFT_BLACK);
  _canvas.setColorDepth(16);

  ledcSetup(cfg::display::BL_LEDC_CHANNEL, cfg::display::BL_LEDC_FREQ_HZ,
            cfg::display::BL_LEDC_RES_BITS);
  ledcAttachPin(cfg::pins::TFT_BACKLIGHT, cfg::display::BL_LEDC_CHANNEL);
  applySettings(settings);
  _lastActivityMs = millis();

  LOGI(TAG, "display ready %ux%u flipped=%s", (unsigned)cfg::display::WIDTH,
       (unsigned)cfg::display::HEIGHT, settings.displayFlipped ? "true" : "false");
  return true;
}

void DisplayManager::applySettings(const AppSettings& settings) {
  DisplayConfig::apply(_tft, settings);
  _dimBrightness = settings.dimBrightness;
  _dimTimeoutS = settings.dimTimeoutS;
  setBrightness(settings.brightness);
}

void DisplayManager::setBrightness(uint8_t level) {
  _brightness = level < cfg::display::MIN_BRIGHTNESS ? cfg::display::MIN_BRIGHTNESS : level;
  if (!_dimmed) writeBacklight(_brightness);
}

bool DisplayManager::noteActivity() {
  _lastActivityMs = millis();
  if (_dimmed) {
    _dimmed = false;
    writeBacklight(_brightness);
    LOGD(TAG, "display woken");
    return true;
  }
  return false;
}

void DisplayManager::tick() {
  if (_dimTimeoutS == 0 || _dimmed) return;
  if (millis() - _lastActivityMs >= (uint32_t)_dimTimeoutS * 1000u) {
    _dimmed = true;
    writeBacklight(_dimBrightness);
    LOGD(TAG, "display dimmed after %u s idle", (unsigned)_dimTimeoutS);
  }
}

void DisplayManager::writeBacklight(uint8_t duty) {
  ledcWrite(cfg::display::BL_LEDC_CHANNEL, duty);
}

TFT_eSprite* DisplayManager::acquireCanvas(uint16_t w, uint16_t h) {
  const uint32_t bytes = (uint32_t)w * (uint32_t)h * 2u;
  if (bytes > cfg::display::CANVAS_MAX_BYTES) {
    LOGE(TAG, "canvas %ux%u exceeds budget (%lu > %lu bytes)", (unsigned)w, (unsigned)h,
         (unsigned long)bytes, (unsigned long)cfg::display::CANVAS_MAX_BYTES);
    return nullptr;
  }
  if (_canvasOk && w == _canvasW && h == _canvasH) return &_canvas;
  if (_canvasOk) {
    _canvas.deleteSprite();
    _canvasOk = false;
  }
  _canvasOk = _canvas.createSprite(w, h) != nullptr;
  if (!_canvasOk) {
    LOGE(TAG, "canvas %ux%u alloc failed (largest free block %lu)", (unsigned)w, (unsigned)h,
         (unsigned long)ESP.getMaxAllocHeap());
    return nullptr;
  }
  _canvasW = w;
  _canvasH = h;
  return &_canvas;
}

void DisplayManager::pushCanvas(int32_t x, int32_t y) {
  if (_canvasOk) _canvas.pushSprite(x, y);
}
