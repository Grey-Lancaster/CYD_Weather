#pragma once

#include <ArduinoJson.h>

#include "storage/SettingsManager.h"

// Full-fidelity JSON (de)serialization of AppSettings, for backup/restore.
// Unlike the web API's live-settings endpoints (WebPortal::handleGetSettings
// et al.), which expose only a UI-relevant subset with secrets masked, this
// covers every persisted field with the real WiFi password and API keys
// included — a restore that can't reconnect to WiFi isn't much of a backup.
// Shared by both backup destinations the web UI offers: a browser download
// (WebPortal's /api/settings/backup + /restore) and an SD card file
// (storage/SdCardManager), so the on-disk format is identical either way.
namespace SettingsBackup {

// Wraps the flattened settings object with a little metadata (device name,
// schema/firmware version the backup was taken on) purely for humans
// inspecting the file and for a sanity check on restore — the fields
// themselves are applied by key, not by position, so an older backup missing
// newer fields just leaves those fields at whatever they currently are.
void toJson(const AppSettings& s, JsonDocument& doc);

// Applies every recognized key found under doc["settings"] onto s. Fields
// absent from doc (e.g. an older backup, or one missing a key by accident)
// are left unchanged. Returns false if doc doesn't look like a settings
// backup at all — checked via the one field every schema this project has
// shipped has always had: settings.wifiSsid.
bool fromJson(const JsonDocument& doc, AppSettings& s);

}  // namespace SettingsBackup
