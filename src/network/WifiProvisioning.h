#pragma once

#include <Arduino.h>

#include <functional>

#include "display/DisplayManager.h"
#include "display/TouchManager.h"

// Bridges TouchWifiProvisioner (an LVGL-based on-device WiFi picker/keyboard)
// onto this project's existing TFT_eSPI display and XPT2046 touch hardware.
// LVGL is scoped entirely to this module and the brief pre-connect
// provisioning phase — see main.cpp's two-phase boot and the README's
// architecture notes for why the rest of the app draws directly via
// TFT_eSPI instead. TouchWifiProvisioner owns WiFi credential storage
// itself (its own NVS namespace, separate from AppSettings) and keeps
// reconnecting on its own after a drop; this project's NetworkManager only
// ever mirrors WiFi status from that point on, never drives the connection.
namespace WifiProvisioning {

using ConnectedCallback = std::function<void(const String& ip)>;

// Brings up LVGL (display flush + touch-indev callbacks wired to `display`
// and `touch`, both assumed already begin()'d) and starts
// TouchWifiProvisioner's picker if no credentials are saved yet, or
// attempts to connect immediately if they are. `onConnected` fires once,
// the first time a connection succeeds — the caller should tear down its
// provisioning-phase loop and start the rest of the app from there.
void begin(DisplayManager& display, TouchManager& touch, const char* hostname,
           ConnectedCallback onConnected);

// Drives LVGL + TouchWifiProvisioner. Call every loop() pass during the
// provisioning phase (i.e. until `onConnected` has fired) — NOT afterward,
// since TouchWifiProvisioner keeps polling reconnection state internally
// even with no UI on screen, and this project's own TaskScheduler-driven
// app loop takes over signaling once connected.
void loop();

// Clears TouchWifiProvisioner's saved credentials (its own NVS namespace,
// separate from AppSettings — see SettingsManager::factoryReset()).
void reset();

}  // namespace WifiProvisioning
