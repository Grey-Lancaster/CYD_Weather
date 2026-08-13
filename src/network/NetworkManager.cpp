#include "network/NetworkManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "utils/Log.h"

static const char* TAG = "NET";

static const char* kStateNames[] = {"Connected", "Disconnected"};

const char* NetworkManager::stateName() const {
  return kStateNames[static_cast<uint8_t>(_state)];
}

void NetworkManager::begin(const AppSettings& settings) {
  strlcpy(_hostname, settings.hostname, sizeof(_hostname));
  tick();  // evaluates the real WiFi.status() now (already connected — see
           // class comment) and fires onConnected()/onStateChange() for it
}

void NetworkManager::applySettings(const AppSettings& settings) {
  // Hostname changes here are cosmetic only (WiFi.setHostname()/mDNS were
  // already applied once at boot by WifiProvisioning/onConnected()) — the
  // web portal already tells the user a restart is required to actually
  // re-advertise a new hostname (see WebPortal::handlePostSettings()).
  strlcpy(_hostname, settings.hostname, sizeof(_hostname));
}

void NetworkManager::onConnected() {
  if (_disconnectedAtMs != 0) {
    LOGI(TAG, "reconnected after %lu ms (outage #%lu)", (unsigned long)(millis() - _disconnectedAtMs),
         (unsigned long)_disconnectCount);
    _disconnectedAtMs = 0;
  }
  LOGI(TAG, "connected, ip=%s rssi=%d hostname=%s", WiFi.localIP().toString().c_str(),
       (int)WiFi.RSSI(), _hostname);
  if (!_mdnsStarted) {
    _mdnsStarted = MDNS.begin(_hostname);
    if (_mdnsStarted) {
      MDNS.addService("http", "tcp", cfg::web::PORT);
      LOGI(TAG, "mDNS: http://%s.local", _hostname);
    } else {
      LOGW(TAG, "mDNS failed to start");
    }
  }
}

void NetworkManager::tick() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const NetState next = connected ? NetState::Connected : NetState::Disconnected;
  if (next == _state) return;
  setState(next);
  if (next == NetState::Connected) onConnected();
}

void NetworkManager::setState(NetState next) {
  if (next == _state) return;
  _state = next;
  LOGI(TAG, "state -> %s", stateName());
  if (next == NetState::Disconnected) {
    _disconnectedAtMs = millis();
    _disconnectCount++;
  }
  if (_onStateChange) _onStateChange(next);
}

String NetworkManager::ssid() const {
  return _state == NetState::Connected ? WiFi.SSID() : String("");
}

String NetworkManager::ipAddress() const {
  return _state == NetState::Connected ? WiFi.localIP().toString() : String("");
}

int16_t NetworkManager::rssi() const {
  return _state == NetState::Connected ? (int16_t)WiFi.RSSI() : 0;
}
