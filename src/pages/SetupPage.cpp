#include "pages/SetupPage.h"

#include "config/AppConfig.h"
#include "ui/Widgets.h"

SetupPage::SetupPage(DisplayManager& display, NetworkManager& network)
    : _display(display), _network(network) {}

void SetupPage::tick(uint32_t nowMs) {
  if (nowMs - _lastRefreshMs < cfg::ui::PAGE_DATA_REFRESH_MS) return;
  _lastRefreshMs = nowMs;
  requestRedraw();
}

void SetupPage::render(bool full) {
  if (full) {
    const Theme& t = theme();
    TFT_eSPI& tft = _display.tft();
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString("Join the WiFi above - the portal opens automatically",
                   cfg::display::WIDTH / 2, ui::layout::HINT_Y);
  }
  renderApCard();
  renderStatusCard();
}

void SetupPage::renderApCard() {
  char portal[40];
  if (_network.isConnected()) {
    snprintf(portal, sizeof(portal), "http://%s.local", _network.hostname());
  } else {
    const String ip = _network.ipAddress();
    if (ip.length() > 0) {
      snprintf(portal, sizeof(portal), "http://%s", ip.c_str());
    } else {
      strlcpy(portal, "-", sizeof(portal));
    }
  }

  ui::renderRegion(_display, ui::layout::CARD1_Y, (uint16_t)ui::layout::CARD_H,
                   [&](TFT_eSPI& gfx, int32_t x0, int32_t y0) {
    using namespace ui::layout;
    const Theme& t = theme();
    ui::drawCard(gfx, x0 + CARD_MARGIN_X, y0, CARD_W, CARD_H, "WiFi Setup");
    const int32_t rx = x0 + CARD_MARGIN_X + t.pad;
    const int32_t rw = CARD_W - 2 * t.pad;
    int32_t ry = y0 + ROWS_Y;
    ui::drawKeyValue(gfx, rx, ry, rw, "AP network", _network.apSsid());
    ry += ROW_H;
    ui::drawKeyValue(gfx, rx, ry, rw, "Password", cfg::net::AP_PASSWORD);
    ry += ROW_H;
    ui::drawKeyValue(gfx, rx, ry, rw, "Portal", portal);
  });
}

void SetupPage::renderStatusCard() {
  char status[48];
  snprintf(status, sizeof(status), "%s", _network.stateName());
  char clients[8];
  snprintf(clients, sizeof(clients), "%u", (unsigned)_network.apClientCount());
  char saved[36];
  if (_network.staSsid()[0] != '\0') {
    strlcpy(saved, _network.staSsid(), sizeof(saved));
  } else {
    strlcpy(saved, "(none)", sizeof(saved));
  }

  ui::renderRegion(_display, ui::layout::CARD2_Y, (uint16_t)ui::layout::CARD_H,
                   [&](TFT_eSPI& gfx, int32_t x0, int32_t y0) {
    using namespace ui::layout;
    const Theme& t = theme();
    ui::drawCard(gfx, x0 + CARD_MARGIN_X, y0, CARD_W, CARD_H, "Status");
    const int32_t rx = x0 + CARD_MARGIN_X + t.pad;
    const int32_t rw = CARD_W - 2 * t.pad;
    int32_t ry = y0 + ROWS_Y;
    ui::drawKeyValue(gfx, rx, ry, rw, "State", status);
    ry += ROW_H;
    ui::drawKeyValue(gfx, rx, ry, rw, "Portal clients", clients);
    ry += ROW_H;
    ui::drawKeyValue(gfx, rx, ry, rw, "Saved WiFi", saved);
  });
}
