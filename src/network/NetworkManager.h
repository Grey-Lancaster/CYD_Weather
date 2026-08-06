#pragma once

#include <Arduino.h>

#include <functional>

#include "config/AppConfig.h"
#include "storage/SettingsManager.h"

enum class NetState : uint8_t { Idle, Connecting, Connected, Disconnected, ApMode };

// Non-blocking WiFi lifecycle as a polled state machine (tick() from the
// scheduler — never blocks):
//   no credentials ──────────────► ApMode ("CYD-xxxx" setup AP)
//   Connecting ── timeout xN ────► ApMode fallback
//   Connecting ◄─ retry delay ──── Disconnected ◄── link lost ── Connected
//   ApMode ── periodic STA retry (cfg::net::AP_STA_RETRY_MS) ──► Connecting
class NetworkManager {
 public:
  using StateCallback = std::function<void(NetState)>;

  void begin(const AppSettings& settings);
  void applySettings(const AppSettings& settings);  // reconnects if creds changed
  void tick();
  void reconnect();

  NetState state() const { return _state; }
  bool isConnected() const { return _state == NetState::Connected; }
  const char* stateName() const;
  String ssid() const;       // active STA ssid, or the AP ssid in ApMode
  String ipAddress() const;  // empty string when there is no address yet
  int16_t rssi() const;
  const char* hostname() const { return _hostname; }
  const char* apSsid() const { return _apSsid; }
  const char* staSsid() const { return _ssid; }  // stored credentials ("" = none)
  uint8_t apClientCount() const;                 // stations joined to the setup AP

  void onStateChange(StateCallback cb) { _onStateChange = cb; }

 private:
  void startConnect();
  void startAccessPoint();
  void onConnected();
  void setState(NetState next);

  char _ssid[33] = {0};
  char _pass[65] = {0};
  char _hostname[24] = {0};
  char _apSsid[33] = {0};
  NetState _state = NetState::Idle;
  uint32_t _stateSinceMs = 0;
  uint32_t _retryAtMs = 0;
  uint8_t _attempts = 0;
  bool _mdnsStarted = false;
  StateCallback _onStateChange;
};
