// ============================================================================
// CYD Weather — composition root.
// Two-phase boot: until WiFi connects, network/WifiProvisioning.h's
// TouchWifiProvisioner-backed on-device picker owns the screen (LVGL) and
// drives loop(). Once connected, startApp() runs the rest of this file's
// original setup() — pages, the scheduler, the web portal — and loop()
// switches over to the cooperative TaskScheduler for good. LVGL is never
// touched again after that handoff; see README's architecture notes for why
// the app itself draws directly via TFT_eSPI instead of LVGL.
// ============================================================================
#include <Arduino.h>

#include "config/AppConfig.h"
#include "config/Version.h"
#include "display/DisplayManager.h"
#include "display/TouchManager.h"
#include "network/NetworkManager.h"
#include "network/TimeManager.h"
#include "network/WifiProvisioning.h"
#include "pages/CurrentConditionsPage.h"
#include "pages/ForecastPage.h"
#include "pages/HourlyForecastPage.h"
#include "pages/PageAlerts.h"
#include "pages/PageDayHourly.h"
#include "pages/PageManager.h"
#include "pages/PageSunMoon.h"
#include "pages/SystemPage.h"
#include "pages/WeatherTrendsPage.h"
#include "storage/SdCardManager.h"
#include "storage/SettingsConsole.h"
#include "storage/SettingsManager.h"
#include "storage/WeatherHistoryManager.h"
#include "ui/StatusBar.h"
#include "ui/Theme.h"
#include "utils/Format.h"
#include "utils/Log.h"
#include "utils/TaskScheduler.h"
#include "web/WebPortal.h"
#include "weather/WeatherAlertManager.h"
#include "weather/WeatherService.h"

static const char* TAG = "MAIN";

// Settings are reached via SettingsManager::instance() (see storage/
// SettingsManager.h) rather than a file-scope global, so no static instance
// is declared here — WebPortal still takes a SettingsManager& via
// constructor injection, it's just handed the singleton.
static DisplayManager displayManager;
static TouchManager touchManager;
static NetworkManager networkManager;
static TimeManager timeManager;
static WeatherAlertManager weatherAlertManager;
static WeatherService weatherService;
static SdCardManager sdCardManager;
static WebPortal webPortal(SettingsManager::instance(), networkManager, weatherService,
                           sdCardManager, displayManager);
static StatusBar statusBar(displayManager, networkManager, weatherAlertManager, weatherService,
                           timeManager);
static PageManager pageManager(displayManager, statusBar);
static ForecastPage forecastPage(displayManager, weatherService);
static PageDayHourly dayHourlyPage(displayManager, weatherService);
static CurrentConditionsPage currentConditionsPage(displayManager, weatherService);
static HourlyForecastPage hourlyForecastPage(displayManager, weatherService);
static WeatherTrendsPage weatherTrendsPage(displayManager);
static PageSunMoon sunMoonPage(displayManager, weatherService);
static PageAlerts alertsPage(displayManager, weatherAlertManager, weatherService);
static SystemPage systemPage(displayManager, networkManager);
static SettingsConsole settingsConsole;
static TaskScheduler scheduler;
// Tracks weatherProvider as of the last-applied settings change, purely so
// webPortal.onSettingsChanged() (below) can tell "the provider changed" from
// "some unrelated setting changed" and only force an immediate refetch for
// the former — set from the loaded settings in startApp(), before that
// callback is ever registered.
static uint8_t lastAppliedWeatherProvider = 0;
static bool appStarted = false;

// Fires once, the first time WiFi connects (see WifiProvisioning::begin()'s
// callback contract) — starts everything that only makes sense once online:
// weather fetching, the page UI, the scheduler, the web portal. Runs from
// inside WifiProvisioning::loop() (via TouchWifiProvisioner's connected
// callback), itself called from loop() below while !appStarted.
static void startApp(const String& ip) {
  if (appStarted) return;
  appStarted = true;
  LOGI(TAG, "WiFi connected (ip=%s), starting app", ip.c_str());

  SettingsManager& settingsManager = SettingsManager::instance();
  const AppSettings& settings = settingsManager.get();

  // Undo WifiProvisioning::begin()'s forced rotation(3)/no-mirror override
  // (see its comment) — restores this app's own configured orientation
  // before any page ever draws.
  displayManager.applySettings(settings);
  touchManager.setProvisioningRotation(false);

  weatherService.begin();
  weatherService.setRefreshInterval(settings.weatherRefreshS);
  WeatherHistoryManager::instance().begin();
  timeManager.begin(settings);
  lastAppliedWeatherProvider = settings.weatherProvider;

  // ForecastPage is added first so it's page index 0 (Page 1) — the device
  // lands directly on it, no splash screen. CurrentConditionsPage is page
  // index 1 (Page 2).
  pageManager.addPage(&forecastPage);
  pageManager.addPage(&currentConditionsPage);
  pageManager.addPage(&hourlyForecastPage);
  pageManager.addPage(&weatherTrendsPage);
  pageManager.addPage(&sunMoonPage);
  pageManager.addPage(&alertsPage);
  // Reached only by tapping a card on ForecastPage (see the onDayTapped
  // wiring below) — not part of the rotation.
  pageManager.addPage(&dayHourlyPage, /*rotatable=*/false);
  // Reached only via the physical BOOT button (see the "boot.button"
  // scheduler task below) — pulled out of the normal swipe/tap rotation so
  // it doesn't show up as one of the dashboard's page-indicator dots.
  pageManager.addPage(&systemPage, /*rotatable=*/false);
  pageManager.setActive((uint8_t)0);
  pageManager.setAutoRotate(settings.autoPageRotation != 0, settings.pageDurationS);

  // Drill-down from a tapped 5-Day-Forecast card into that day's hourly
  // breakdown, and back again (this page doesn't hold a PageManager/
  // PageDayHourly reference itself).
  forecastPage.onDayTapped([](uint8_t dayIndex) {
    dayHourlyPage.setDayIndex(dayIndex);
    pageManager.setActive(&dayHourlyPage);
  });
  dayHourlyPage.onBack([]() { pageManager.setActive(&forecastPage); });

  // A touch that wakes a dimmed display is consumed (no accidental navigation).
  touchManager.onActivity([]() { return displayManager.noteActivity(); });
  touchManager.onTap([](TouchManager::Point p) { pageManager.handleTap(p.x, p.y); });
  touchManager.onSwipe([](TouchManager::SwipeDirection dir) {
    // Some touch controller variants report X backwards, which flips which
    // physical swipe direction the gesture math (TouchManager.cpp) resolves
    // to Left vs. Right — swapNavigation compensates by inverting which one
    // advances vs. goes back.
    bool advance = dir == TouchManager::SwipeDirection::Left;
    if (SettingsManager::instance().get().swapNavigation) advance = !advance;
    if (advance) {
      pageManager.next();
    } else {
      pageManager.prev();
    }
  });
  webPortal.onSettingsChanged([]() {
    const AppSettings& current = SettingsManager::instance().get();
    Log::setLevel(static_cast<LogLevel>(current.logLevel));
    displayManager.applySettings(current);
    touchManager.applySettings(current);
    networkManager.applySettings(current);
    setActiveTheme(current.theme);
    pageManager.setAutoRotate(current.autoPageRotation != 0, current.pageDurationS);
    pageManager.requestFullRedraw();  // theme colors won't repaint via dirty flags alone
    weatherService.setRefreshInterval(current.weatherRefreshS);
    if (current.weatherProvider != lastAppliedWeatherProvider) {
      lastAppliedWeatherProvider = current.weatherProvider;
      weatherService.forceRefetch();  // otherwise the switch is invisible for up to weatherRefreshS
    }
    timeManager.applySettings(current);  // re-syncs immediately if server/timezone changed
  });
  // Kicks off NTP sync on every Connected transition, not just this first
  // one — a reconnect after a WiFi outage doubles as a retry if the last
  // sync never landed. Registered before networkManager.begin() so this
  // very first (already-true) transition fires it too.
  networkManager.onStateChange([](NetState state) {
    if (state == NetState::Connected) timeManager.requestSync();
  });
  networkManager.begin(settings);

  webPortal.begin();

  scheduler.add("web.handle", cfg::sched::WEB_HANDLE_MS, []() { webPortal.handle(); });
  scheduler.add("touch.poll", cfg::sched::TOUCH_POLL_MS, []() { touchManager.poll(); });
  // Physical BOOT button (GPIO0, active-low via INPUT_PULLUP): a short press
  // is the only way to reach System Information now that it's out of the
  // normal page rotation (see pageManager.addPage(&systemPage, ...) above);
  // calling setActive() again while already on that page is a harmless
  // no-op, so no extra debounce handling is needed beyond the natural ~20ms
  // poll step. Holding it for FACTORY_RESET_HOLD_MS wipes settings *and*
  // the WiFi credentials TouchWifiProvisioner stores separately (see
  // WifiProvisioning::reset() — otherwise they'd survive a factory reset as
  // a surprising leftover) and reboots.
  scheduler.add("boot.button", cfg::sched::BOOT_BUTTON_POLL_MS, []() {
    static bool wasPressed = false;
    static uint32_t pressedAtMs = 0;
    static bool resetFired = false;
    const bool pressed = digitalRead(cfg::pins::BOOT_BUTTON) == LOW;
    if (pressed && !wasPressed) {
      pageManager.setActive(&systemPage);
      pressedAtMs = millis();
      resetFired = false;
    } else if (pressed && !resetFired &&
               millis() - pressedAtMs >= cfg::sys::FACTORY_RESET_HOLD_MS) {
      resetFired = true;
      LOGW(TAG, "boot button held %lu ms, factory resetting",
           (unsigned long)cfg::sys::FACTORY_RESET_HOLD_MS);
      SettingsManager::instance().factoryReset();
      WifiProvisioning::reset();
      Serial.flush();
      ESP.restart();
    }
    wasPressed = pressed;
  });
  scheduler.add("ui.render", cfg::sched::RENDER_MS, []() { pageManager.render(); });
  scheduler.add("ui.tick", cfg::sched::UI_TICK_MS, []() {
    pageManager.tick(millis());
    displayManager.tick();
  });
  scheduler.add("net.tick", cfg::sched::NET_TICK_MS, []() { networkManager.tick(); });
  scheduler.add("time.tick", cfg::sched::TIME_TICK_MS, []() { timeManager.tick(millis()); });
  scheduler.add("weather.tick", cfg::sched::WEATHER_TICK_MS, []() {
    weatherService.tick(millis(), networkManager.isConnected());
  });
  // Bridges WeatherService -> WeatherHistoryManager: the only place that
  // knows about both, so neither has to know about the other (see
  // storage/WeatherHistoryManager.h). Rate-limiting/rotation/etc. all live
  // in the manager itself — this just notices a new fetch landed and maps
  // it into the manager's own reading struct.
  scheduler.add("history.log", cfg::sched::HISTORY_TICK_MS, []() {
    static uint32_t lastSeenVersion = 0;
    const uint32_t version = weatherService.dataVersion();
    if (version == lastSeenVersion) return;
    lastSeenVersion = version;

    const WeatherData d = weatherService.data();
    if (!d.valid) return;

    WeatherHistoryReading reading;
    strlcpy(reading.timestamp, d.isoTime, sizeof(reading.timestamp));
    reading.temperatureC = d.temperatureC;
    reading.humidityPct = d.humidityPct;
    reading.pressureHpa = d.pressureHpa;
    reading.windSpeedKmh = d.windSpeedKmh;
    reading.rainProbabilityPct = d.hourlyCount > 0 ? d.hourly[0].rainProbabilityPct : 0;
    reading.cloudCoverPct = d.cloudCoverPct;
    reading.weatherCode = d.weatherCode;
    WeatherHistoryManager::instance().addReading(reading);
  });
  // WeatherAlertManager::evaluate() does real work (scans the hourly array,
  // reads back recent history for the pressure trend) — only run it when a
  // fetch actually landed, same version-watch idea as "history.log" above,
  // not on a timer (see weather/WeatherAlertManager.h).
  scheduler.add("alerts.eval", cfg::sched::ALERTS_TICK_MS, []() {
    static uint32_t lastSeenVersion = 0;
    const uint32_t version = weatherService.dataVersion();
    if (version == lastSeenVersion) return;
    lastSeenVersion = version;
    weatherAlertManager.evaluate(weatherService.data());
  });
  scheduler.add("settings.console", cfg::sched::CONSOLE_POLL_MS, []() { settingsConsole.poll(); });
  scheduler.add("sys.stats", cfg::sched::STATS_MS, []() {
    char up[24];
    fmt::uptime(up, sizeof(up), millis());
    const uint32_t heapFree = ESP.getFreeHeap();
    LOGI("SYS", "up %s | heap free=%lu min=%lu maxblock=%lu", up, (unsigned long)heapFree,
         (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
    if (heapFree < cfg::sys::LOW_HEAP_WARN_BYTES) {
      LOGW("SYS", "low heap: %lu bytes free", (unsigned long)heapFree);
    }
  });

  // Paint the dashboard now rather than waiting for the first scheduler
  // pass — closes the brief (~1 loop) window where the backlight is already
  // on but nothing has been drawn yet.
  pageManager.render();

  LOGI(TAG, "ready: %u tasks, free heap %lu", (unsigned)scheduler.count(),
       (unsigned long)ESP.getFreeHeap());
}

void setup() {
  Log::begin(cfg::serial::BAUD);
  LOGI(TAG, "%s v%s (built %s)", FW_NAME, FW_VERSION, FW_BUILD_DATE);

  SettingsManager& settingsManager = SettingsManager::instance();
  settingsManager.loadSettings();
  const AppSettings& settings = settingsManager.get();
  Log::setLevel(static_cast<LogLevel>(settings.logLevel));
  setActiveTheme(settings.theme);

  displayManager.begin(settings);
  touchManager.begin(settings);
  // SD card and touch share a physical SPI peripheral on different pin
  // mappings (see storage/SdCardManager's class comment) — hand the bus back
  // to touch immediately after any SD access.
  sdCardManager.onBusReleased([]() { touchManager.reacquireBus(); });
  pinMode(cfg::pins::BOOT_BUTTON, INPUT_PULLUP);

  // WiFi provisioning owns the screen (LVGL) until connected — see
  // network/WifiProvisioning.h and this file's own top comment. startApp()
  // runs everything else once that happens.
  WifiProvisioning::begin(displayManager, touchManager, settings.hostname, startApp);
}

void loop() {
  if (!appStarted) {
    WifiProvisioning::loop();
  } else {
    scheduler.loop();
  }
}
