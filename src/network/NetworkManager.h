#pragma once

#include <Arduino.h>

#include <functional>

#include "config/AppConfig.h"
#include "storage/SettingsManager.h"

enum class NetState : uint8_t { Connected, Disconnected };

// Thin WiFi status mirror — by the time begin() runs, WifiProvisioning (see
// network/WifiProvisioning.h) has already established the connection during
// the app's pre-app provisioning phase, and keeps reconnecting on its own
// after a drop (TouchWifiProvisioner's own background logic). This class
// never drives the connection itself; it only polls WiFi.status() for the
// rest of the app (StatusBar's dot colour, SystemPage, the web API's
// /api/status) and starts mDNS once, the first time it observes a connection.
class NetworkManager {
 public:
  using StateCallback = std::function<void(NetState)>;

  void begin(const AppSettings& settings);
  void applySettings(const AppSettings& settings);  // hostname only; see .cpp
  void tick();

  NetState state() const { return _state; }
  bool isConnected() const { return _state == NetState::Connected; }
  const char* stateName() const;
  String ssid() const;
  String ipAddress() const;  // empty string when there is no address yet
  int16_t rssi() const;
  const char* hostname() const { return _hostname; }

  void onStateChange(StateCallback cb) { _onStateChange = cb; }

 private:
  void setState(NetState next);
  void onConnected();

  char _hostname[24] = {0};
  NetState _state = NetState::Disconnected;
  bool _mdnsStarted = false;
  StateCallback _onStateChange;
};
