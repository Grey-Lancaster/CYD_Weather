#include "storage/SettingsConsole.h"

#include "storage/SettingsManager.h"
#include "utils/Log.h"

static const char* TAG = "CONSOLE";
static constexpr size_t MAX_LINE_LEN = 96;  // guards unbounded growth if no '\n' ever arrives

void SettingsConsole::poll() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleLine(_buf);
      _buf = "";
      continue;
    }
    if (_buf.length() < MAX_LINE_LEN) _buf += c;
  }
}

void SettingsConsole::handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd = (sp < 0 ? line : line.substring(0, sp));
  String rest = (sp < 0 ? String("") : line.substring(sp + 1));
  cmd.toLowerCase();

  SettingsManager& mgr = SettingsManager::instance();

  if (cmd == "settings") {
    mgr.printSettings();
  } else if (cmd == "save") {
    Serial.println(mgr.saveSettings() ? "OK: settings saved" : "ERROR: save failed");
  } else if (cmd == "load") {
    Serial.println(mgr.loadSettings() ? "OK: settings loaded" : "ERROR: load failed (using defaults)");
  } else if (cmd == "reset") {
    Serial.println(mgr.resetSettings() ? "OK: settings reset to defaults" : "ERROR: reset failed");
  } else if (cmd == "get") {
    rest.trim();
    if (rest.length() == 0) {
      Serial.println("usage: get <key>");
    } else {
      Serial.printf("%s = %s\r\n", rest.c_str(), mgr.getSetting(rest).c_str());
    }
  } else if (cmd == "set") {
    rest.trim();
    const int sp2 = rest.indexOf(' ');
    if (sp2 < 0) {
      Serial.println("usage: set <key> <value>");
    } else {
      const String key = rest.substring(0, sp2);
      String value = rest.substring(sp2 + 1);
      value.trim();
      if (mgr.setSetting(key, value)) {
        Serial.printf("OK: %s = %s (call 'save' to persist)\r\n", key.c_str(), value.c_str());
      } else {
        Serial.printf("ERROR: unknown key '%s'\r\n", key.c_str());
      }
    }
  } else {
    LOGW(TAG, "unknown command '%s' (try: settings, get, set, save, load, reset)", cmd.c_str());
  }
}
