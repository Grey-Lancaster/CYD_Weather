#include "network/WifiProvisioning.h"

#include <TouchWifiProvisioner.h>
#include <lvgl.h>

#include "config/AppConfig.h"
#include "utils/Log.h"

static const char* TAG = "WIFIPROV";

namespace WifiProvisioning {
namespace {

DisplayManager* g_display = nullptr;
TouchManager* g_touch = nullptr;
ConnectedCallback g_onConnected;
bool g_firedConnected = false;

// One 10-row-tall draw buffer (320 * 10 * 2 bytes = 6.4KB) — shrunk from
// TouchWifiProvisioner's own CYD_BasicConnect example (40 rows, 25.6KB):
// this project's own static buffers already use most of the ESP32's DRAM
// budget, and the linker didn't have room for the example's larger one
// (confirmed live: 22KB DRAM overflow before this and LV_MEM_SIZE's own
// shrink in lv_conf.h). Single-buffered (LVGL blocks on flush before
// continuing) either way — fine here, this only ever drives a simple
// picker/keyboard UI for a few seconds at boot, not animation-heavy
// content, so the extra flush calls a smaller buffer costs are irrelevant.
lv_disp_draw_buf_t g_drawBuf;
lv_color_t g_buf1[cfg::display::WIDTH * 10];

void dispFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  TFT_eSPI& tft = g_display->tft();
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  // Absolute panel coordinates: the UI sits at ORIGIN_X/Y on panels larger than
  // 320x240 (0,0 on the 2.8" boards).
  tft.setAddrWindow(area->x1 + cfg::display::ORIGIN_X, area->y1 + cfg::display::ORIGIN_Y, w, h);
  tft.pushColors((uint16_t*)&colorP->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

void touchpadRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  TouchManager::Point p;
  if (g_touch->rawTouchProvisioning(p)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = p.x;
    data->point.y = p.y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

}  // namespace

void begin(DisplayManager& display, TouchManager& touch, const char* hostname,
           ConnectedCallback onConnected) {
  g_display = &display;
  g_touch = &touch;
  g_onConnected = onConnected;
  g_firedConnected = false;

  // Force the exact orientation TouchWifiProvisioner's own tested-working
  // example uses (plain rotation 3, no MADCTL mirror bits) rather than
  // whatever this app's own displayFlipped/mirrorH/mirrorV settings say —
  // see rawTouchProvisioning()'s header comment. This overrides whatever
  // DisplayManager::begin() already applied for the main app; startApp()
  // restores it via DisplayManager::applySettings() once connected.
  if (!touch.usesCalibration()) {
    display.tft().setRotation(3);
    touch.setProvisioningRotation(true);
  }
  // With a stored touch calibration the picker simply follows the app's own display
  // orientation (the calibration was made in it), so nothing is forced above.
#if defined(BOARD_CYD35)
  // The picker flushes in absolute panel coordinates (see dispFlush), so leave the
  // UI viewport off and clear the border.
  display.resetViewport();
  display.tft().fillScreen(TFT_BLACK);
#endif

  lv_init();

  lv_disp_draw_buf_init(&g_drawBuf, g_buf1, nullptr, cfg::display::WIDTH * 10);
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = cfg::display::WIDTH;
  dispDrv.ver_res = cfg::display::HEIGHT;
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &g_drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchpadRead;
  lv_indev_drv_register(&indevDrv);

  LOGI(TAG, "starting picker (hostname=%s)", hostname);
  TouchWifiProvisioner::begin(
      lv_scr_act(), hostname,
      [](const String& ip) {
        // TouchWifiProvisioner also calls this again after every reconnect
        // following a later drop (see its header comment) — only the first
        // call matters here, it's what ends the provisioning phase.
        if (g_firedConnected) return;
        g_firedConnected = true;
        LOGI(TAG, "connected, ip=%s", ip.c_str());
        if (g_onConnected) g_onConnected(ip);
      });
}

void loop() {
  lv_timer_handler();
  TouchWifiProvisioner::loop();
}

void reset() { TouchWifiProvisioner::reset(); }

}  // namespace WifiProvisioning
