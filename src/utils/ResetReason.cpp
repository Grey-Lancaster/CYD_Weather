#include "utils/ResetReason.h"

#include <esp_system.h>

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Power on";
    case ESP_RST_EXT: return "Reset button";
    case ESP_RST_SW: return "Restart (software)";
    case ESP_RST_PANIC: return "Crash (panic)";
    case ESP_RST_INT_WDT: return "Crash (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "Hang (watchdog restarted it)";
    case ESP_RST_WDT: return "Hang (watchdog)";
    case ESP_RST_BROWNOUT: return "Brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    default: return "Unknown";
  }
}
