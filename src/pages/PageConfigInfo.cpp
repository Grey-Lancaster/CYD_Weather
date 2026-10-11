#include "pages/PageConfigInfo.h"

#include "ui/Widgets.h"

namespace {
constexpr int32_t kTitleY = ui::layout::BODY_Y + 6;
constexpr int32_t kHintY = ui::layout::BODY_Y + 40;
constexpr int32_t kIpY = ui::layout::BODY_Y + 62;
constexpr int32_t kOrY = ui::layout::BODY_Y + 96;
constexpr int32_t kHostY = ui::layout::BODY_Y + 112;
constexpr int32_t kBootY = ui::layout::BODY_Y + 152;
constexpr int32_t kCountdownY = ui::layout::BODY_Y + 174;
}  // namespace

PageConfigInfo::PageConfigInfo(DisplayManager& display, NetworkManager& network)
    : _display(display), _network(network) {}

void PageConfigInfo::onEnter() {
  Page::onEnter();
  _enteredMs = millis();
  _secondsLeft = (int32_t)cfg::ui::CONFIG_INFO_S;
  _finished = false;
}

void PageConfigInfo::render(bool full) {
  TFT_eSPI& tft = _display.tft();
  const Theme& t = theme();
  const int32_t cx = (int32_t)cfg::display::WIDTH / 2;

  if (full) {
    tft.fillRect(0, ui::layout::BODY_Y, cfg::display::WIDTH,
                 (int32_t)cfg::display::HEIGHT - ui::layout::BODY_Y, t.bg);
    tft.setTextDatum(TC_DATUM);

    tft.setTextFont(t.fontLarge);
    tft.setTextColor(t.ok, t.bg);
    tft.drawString("Connected to WiFi", cx, kTitleY);

    tft.setTextFont(t.fontSmall);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString("Configure it here - open in a browser:", cx, kHintY);

    String ip = _network.ipAddress();
    if (ip.length() == 0) ip = "(no address yet)";
    tft.setTextFont(t.fontLarge);
    tft.setTextColor(t.textPrimary, t.bg);
    tft.drawString(ip, cx, kIpY);

    tft.setTextFont(t.fontSmall);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString("or", cx, kOrY);

    char host[48];
    snprintf(host, sizeof(host), "%s.local", _network.hostname());
    tft.setTextFont(t.fontLarge);
    tft.setTextColor(t.textPrimary, t.bg);
    tft.drawString(host, cx, kHostY);

    tft.setTextFont(t.fontSmall);
    tft.setTextColor(t.textSecondary, t.bg);
    tft.drawString("Press BOOT any time to see this address", cx, kBootY);
  }

  // Countdown line: redrawn every second, clearing its row first so a shorter
  // number never leaves ghost digits behind.
  tft.fillRect(0, kCountdownY, cfg::display::WIDTH, 18, t.bg);
  char line[40];
  snprintf(line, sizeof(line), "Starting in %ld s  (tap to skip)", (long)_secondsLeft);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(t.fontSmall);
  tft.setTextColor(t.accent, t.bg);
  tft.drawString(line, cx, kCountdownY);
}

bool PageConfigInfo::handleTap(int16_t, int16_t) {
  finish();
  return true;  // consumed: don't also advance to the next page
}

void PageConfigInfo::tick(uint32_t nowMs) {
  if (_finished) return;
  const uint32_t elapsedMs = nowMs - _enteredMs;
  const uint32_t totalMs = (uint32_t)cfg::ui::CONFIG_INFO_S * 1000u;
  if (elapsedMs >= totalMs) {
    finish();
    return;
  }
  const int32_t left = (int32_t)((totalMs - elapsedMs + 999u) / 1000u);
  if (left != _secondsLeft) {
    _secondsLeft = left;
    requestRedraw();
  }
}

void PageConfigInfo::finish() {
  if (_finished) return;
  _finished = true;
  if (_onDone) _onDone();
}
