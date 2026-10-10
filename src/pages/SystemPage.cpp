#include "pages/SystemPage.h"

#include "config/Version.h"
#include "storage/WeatherHistoryManager.h"
#include "ui/Widgets.h"
#include "utils/Format.h"
#include "utils/ResetReason.h"

namespace {
constexpr uint8_t kRows = 10;
constexpr int32_t kRowH = ui::layout::BODY_H / kRows;
constexpr int32_t kLabelX = ui::layout::CARD_MARGIN_X + 10;
constexpr int32_t kValueX = ui::layout::CARD_MARGIN_X + ui::layout::CARD_W - 10;
// Value column redraws every refresh tick; clearing it first (rather than
// relying on drawString's own background fill) avoids ghosting when a new
// value is narrower than the old one it replaces (e.g. RSSI losing a digit).
constexpr int32_t kValueColX = ui::layout::CARD_MARGIN_X + ui::layout::CARD_W / 2;
constexpr int32_t kValueColW =
    ui::layout::CARD_MARGIN_X + ui::layout::CARD_W - kValueColX - 4;
constexpr const char* kLabels[kRows] = {"Firmware",     "IP Address",     "WiFi Signal",
                                        "Free Heap",    "Uptime",         "Storage Total",
                                        "Storage Used", "Readings Stored", "Last Logged",
                                        "Last Restart"};

int32_t rowMidY(uint8_t i) {
  return ui::layout::BODY_Y + (int32_t)i * kRowH + kRowH / 2;
}
}  // namespace

SystemPage::SystemPage(DisplayManager& display, NetworkManager& network)
    : _display(display), _network(network) {}

void SystemPage::tick(uint32_t nowMs) {
  // Heap/uptime/signal change continuously; a cheap 1 Hz refresh keeps the
  // page live without per-field delta tracking.
  if (nowMs - _lastRefreshMs < cfg::ui::PAGE_DATA_REFRESH_MS) return;
  _lastRefreshMs = nowMs;
  requestRedraw();
}

void SystemPage::render(bool full) {
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();

  if (full) {
    ui::drawPageTitle(tft, "System Information");
    tft.fillRoundRect(ui::layout::CARD_MARGIN_X, ui::layout::BODY_Y, ui::layout::CARD_W,
                      ui::layout::BODY_H, t.cardRadius, t.surface);
    tft.drawRoundRect(ui::layout::CARD_MARGIN_X, ui::layout::BODY_Y, ui::layout::CARD_W,
                      ui::layout::BODY_H, t.cardRadius, t.border);
    tft.setTextFont(t.fontSmall);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(t.textSecondary, t.surface);
    for (uint8_t i = 0; i < kRows; i++) {
      tft.drawString(kLabels[i], kLabelX, rowMidY(i));
      if (i > 0) {
        tft.drawFastHLine(ui::layout::CARD_MARGIN_X, ui::layout::BODY_Y + i * kRowH,
                          ui::layout::CARD_W, t.border);
      }
    }
  }

  char ip[24];
  strlcpy(ip, _network.ipAddress().c_str(), sizeof(ip));
  if (ip[0] == '\0') strlcpy(ip, "-", sizeof(ip));

  char signal[24];
  if (_network.isConnected()) {
    snprintf(signal, sizeof(signal), "%d dBm", (int)_network.rssi());
  } else {
    strlcpy(signal, _network.stateName(), sizeof(signal));
  }

  char heap[16];
  fmt::bytes(heap, sizeof(heap), ESP.getFreeHeap());

  char up[24];
  fmt::uptime(up, sizeof(up), millis());

  const WeatherHistoryManager::StorageUsage storage =
      WeatherHistoryManager::instance().getStorageUsage();
  char storageTotal[16];
  char storageUsed[16];
  char readingCount[12];
  char lastLogged[8];
  fmt::bytes(storageTotal, sizeof(storageTotal), (uint32_t)storage.totalBytes);
  fmt::bytes(storageUsed, sizeof(storageUsed), (uint32_t)storage.usedBytes);
  snprintf(readingCount, sizeof(readingCount), "%u", (unsigned)storage.readingCount);
  // storage.lastLoggedTime is "YYYY-MM-DDTHH:MM" (see WeatherHistoryManager) —
  // this column only has room for the time, same convention as the other
  // pages' "Updated HH:MM" captions.
  const char* loggedClock = strchr(storage.lastLoggedTime, 'T');
  strlcpy(lastLogged, loggedClock != nullptr ? loggedClock + 1 : "-", sizeof(lastLogged));

  const char* values[kRows] = {"v" FW_VERSION, ip,          signal,       heap,       up,
                               storageTotal,   storageUsed, readingCount, lastLogged,
                               resetReasonText()};
  tft.setTextFont(t.fontSmall);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(t.textPrimary, t.surface);
  for (uint8_t i = 0; i < kRows; i++) {
    const int32_t y = ui::layout::BODY_Y + i * kRowH;
    tft.fillRect(kValueColX, y + 1, kValueColW, kRowH - 1, t.surface);
    tft.drawString(values[i], kValueX, rowMidY(i));
  }
}
