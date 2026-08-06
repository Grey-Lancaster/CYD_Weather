#pragma once

#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "storage/WeatherHistoryManager.h"

// Page 4 — trend graphs (temperature, humidity, pressure, wind speed, rain
// probability) over the last 24 hours of *locally logged* readings (see
// storage/WeatherHistoryManager.h) — no new API request, no new storage,
// just reading back what WeatherHistoryManager already logs every 15
// minutes. The selected graph can be changed by tapping one of the footer
// tabs, or left alone to auto-cycle — see tick()/handleTap().
class WeatherTrendsPage : public Page {
 public:
  explicit WeatherTrendsPage(DisplayManager& display);

  const char* name() const override { return "trends"; }
  void render(bool full) override;
  void tick(uint32_t nowMs) override;
  bool handleTap(int16_t x, int16_t y) override;

  // Public so the free helper functions in WeatherTrendsPage.cpp (graphName(),
  // graphTabLabel()) can name it — this page's own small vocabulary, not
  // meant to be used outside this page/file, but C++ access rules require it.
  enum class Graph : uint8_t { Temperature, Humidity, Pressure, WindSpeed, RainProbability, Count };

 private:
  DisplayManager& _display;
  Graph _selected = Graph::Temperature;  // default view per this page's brief
  uint32_t _lastAutoAdvanceMs = 0;
  size_t _lastSeenReadingCount = 0;

  // Reused across renders rather than stack-allocated inside render() —
  // keeps a ~140-point history read (see kMaxPoints in the .cpp) off the
  // main loop task's stack. Populated fresh every render() call, not
  // persisted/cached data in its own right.
  static constexpr size_t kMaxPoints = 96;  // 24h at the log's 15-min floor
  WeatherHistoryReading _readings[kMaxPoints];
  float _plotValues[kMaxPoints];
};
