#include "network/TimeManager.h"

#include <esp_sntp.h>
#include <time.h>

#include "utils/Log.h"

static const char* TAG = "TIME";

namespace {
// A correctly-synced clock reads well past this (2024-01-01 00:00:00 UTC);
// the pre-sync default is the Unix epoch itself, so this cheaply tells the
// two apart without reaching into ESP-IDF's sntp_get_sync_status().
constexpr time_t kSaneEpoch = 1704067200;
}  // namespace

void TimeManager::begin(const AppSettings& settings) {
  strlcpy(_server, settings.ntpServer, sizeof(_server));
  strlcpy(_tz, settings.timezone, sizeof(_tz));
}

void TimeManager::applySettings(const AppSettings& settings) {
  const bool changed =
      strcmp(_server, settings.ntpServer) != 0 || strcmp(_tz, settings.timezone) != 0;
  strlcpy(_server, settings.ntpServer, sizeof(_server));
  strlcpy(_tz, settings.timezone, sizeof(_tz));
  if (changed) requestSync();
}

void TimeManager::requestSync() {
  if (_server[0] == '\0') return;
  // Sets the TZ environment variable (so localtime()/strftime() come out
  // already DST-adjusted) and queues an SNTP request against _server; both
  // steps are fire-and-forget, safe to call again before the first one lands.
  configTzTime(_tz, _server);
  // ESP-IDF's SNTP client re-syncs on its own internal timer (1h default) in
  // addition to the explicit requests this class makes, so left alone it
  // hits _server far more often than RESYNC_INTERVAL_S implies. Aligning its
  // interval to ours and restarting makes this class the only scheduler.
  sntp_set_sync_interval(cfg::ntp::RESYNC_INTERVAL_S * 1000u);
  sntp_restart();
  _pendingSync = true;
  _lastAttemptMs = millis();
  LOGI(TAG, "NTP sync requested (%s, tz=%s)", _server, _tz);
}

void TimeManager::tick(uint32_t nowMs) {
  if (_pendingSync) {
    if (time(nullptr) > kSaneEpoch) {
      _pendingSync = false;
      _synced = true;
      LOGI(TAG, "NTP synced");
    } else if (nowMs - _lastAttemptMs >= cfg::ntp::SYNC_TIMEOUT_MS) {
      _pendingSync = false;
      LOGW(TAG, "NTP sync timed out (will retry on next reconnect/24h cycle)");
    }
    return;
  }
  if (nowMs - _lastAttemptMs >= cfg::ntp::RESYNC_INTERVAL_S * 1000u) {
    requestSync();
  }
}
