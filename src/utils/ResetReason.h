#pragma once

// Why the chip last restarted: "Power on", "Reset button", "Restart (software)",
// "Crash (panic)", "Hang (watchdog restarted it)", "Brownout (power dip)", ...
// Shown on the System page and in /api/status so crashes can be spotted
// without a serial cable.
const char* resetReasonText();
