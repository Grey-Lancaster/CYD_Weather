#pragma once

#include <Arduino.h>

// Minimal line-based serial command console for exercising the persisted
// settings store (storage/SettingsManager.h) without the web portal.
// Reads SettingsManager::instance() directly, so it needs nothing wired in
// beyond a poll() call on a scheduler tick.
//
// Commands (newline-terminated, case-insensitive):
//   settings          print every stored value (password masked)
//   get <key>         print one field, e.g. "get brightness"
//   set <key> <value> update one field in memory, e.g. "set brightness 200"
//   save              persist the in-memory settings to NVS
//   load              reload settings from NVS, discarding unsaved edits
//   reset             factory-reset settings to defaults and persist
class SettingsConsole {
 public:
  void poll();  // call every loop pass (or a short scheduler cadence)

 private:
  void handleLine(String line);

  String _buf;
};
