#include "network/NetworkManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "utils/Log.h"

static const char* TAG = "NET";

static const char* kStateNames[] = {"Idle", "Connecting", "Connected", "Disconnected",
                                    "AP mode"};

const char* NetworkManager::stateName() const {
  return kStateNames[static_cast<uint8_t>(_state)];
}

void NetworkManager::begin(const AppSettings& settings) {
  strlcpy(_ssid, settings.wifiSsid, sizeof(_ssid));
  strlcpy(_pass, settings.wifiPass, sizeof(_pass));
  strlcpy(_hostname, settings.hostname, sizeof(_hostname));
  snprintf(_apSsid, sizeof(_apSsid), "%s%04X", cfg::net::AP_SSID_PREFIX,
           (unsigned)((uint16_t)(ESP.getEfuseMac() >> 32)));

  WiFi.persistent(false);       // credentials live in our own settings blob
  WiFi.setAutoReconnect(false); // reconnect policy is owned by this class

  if (_ssid[0] == '\0') {
    LOGW(TAG, "no WiFi credentials, starting setup AP");
    startAccessPoint();
  } else {
    _attempts = 0;
    startConnect();
  }
}

void NetworkManager::applySettings(const AppSettings& settings) {
  const bool changed = strcmp(_ssid, settings.wifiSsid) != 0 ||
                       strcmp(_pass, settings.wifiPass) != 0 ||
                       strcmp(_hostname, settings.hostname) != 0;
  strlcpy(_ssid, settings.wifiSsid, sizeof(_ssid));
  strlcpy(_pass, settings.wifiPass, sizeof(_pass));
  strlcpy(_hostname, settings.hostname, sizeof(_hostname));
  if (changed) {
    LOGI(TAG, "network settings changed, reconnecting");
    reconnect();
  }
}

void NetworkManager::reconnect() {
  _attempts = 0;
  if (_ssid[0] == '\0') {
    startAccessPoint();
  } else {
    startConnect();
  }
}

void NetworkManager::startConnect() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(_hostname);
  WiFi.begin(_ssid, _pass);
  _attempts++;
  setState(NetState::Connecting);
  _stateSinceMs = millis();  // ensure the timeout window restarts even on re-entry
  LOGI(TAG, "connecting to '%s' (attempt %u/%u)", _ssid, (unsigned)_attempts,
       (unsigned)cfg::net::MAX_CONNECT_ATTEMPTS);
}

void NetworkManager::startAccessPoint() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(_apSsid, cfg::net::AP_PASSWORD);
  setState(NetState::ApMode);
  _stateSinceMs = millis();
  LOGW(TAG, "AP '%s' up, portal at http://%s", _apSsid,
       WiFi.softAPIP().toString().c_str());
}

void NetworkManager::onConnected() {
  _attempts = 0;
  setState(NetState::Connected);
  LOGI(TAG, "connected, ip=%s rssi=%d hostname=%s",
       WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), _hostname);
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
  const uint32_t now = millis();
  switch (_state) {
    case NetState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        onConnected();
      } else if (now - _stateSinceMs >= cfg::net::CONNECT_TIMEOUT_MS) {
        if (_attempts >= cfg::net::MAX_CONNECT_ATTEMPTS) {
          LOGE(TAG, "giving up after %u attempts, falling back to AP", (unsigned)_attempts);
          startAccessPoint();
        } else {
          WiFi.disconnect();
          _retryAtMs = now + cfg::net::RETRY_DELAY_MS;
          setState(NetState::Disconnected);
        }
      }
      break;
    case NetState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        LOGW(TAG, "connection lost");
        _retryAtMs = now + cfg::net::RETRY_DELAY_MS;
        setState(NetState::Disconnected);
      }
      break;
    case NetState::Disconnected:
      if ((int32_t)(now - _retryAtMs) >= 0) startConnect();
      break;
    case NetState::ApMode:
      // Never abandon the AP while someone is connected to it configuring.
      if (WiFi.softAPgetStationNum() > 0) {
        _stateSinceMs = now;
        break;
      }
      if (_ssid[0] != '\0' && cfg::net::AP_STA_RETRY_MS > 0 &&
          now - _stateSinceMs >= cfg::net::AP_STA_RETRY_MS) {
        LOGI(TAG, "retrying STA connection from AP mode");
        _attempts = 0;
        startConnect();
      }
      break;
    case NetState::Idle:
    default:
      break;
  }
}

void NetworkManager::setState(NetState next) {
  if (next == _state) return;
  _state = next;
  _stateSinceMs = millis();
  LOGD(TAG, "state -> %s", stateName());
  if (_onStateChange) _onStateChange(next);
}

String NetworkManager::ssid() const {
  if (_state == NetState::ApMode) return String(_apSsid);
  return String(_ssid);
}

String NetworkManager::ipAddress() const {
  if (_state == NetState::ApMode) return WiFi.softAPIP().toString();
  if (_state == NetState::Connected) return WiFi.localIP().toString();
  return String("");
}

int16_t NetworkManager::rssi() const {
  return _state == NetState::Connected ? (int16_t)WiFi.RSSI() : 0;
}

uint8_t NetworkManager::apClientCount() const {
  return _state == NetState::ApMode ? (uint8_t)WiFi.softAPgetStationNum() : 0;
}
