#pragma once

#include <Arduino.h>

#include <functional>

#include "config/AppConfig.h"

// Cooperative, non-preemptive task scheduler.
//  - Fixed capacity (cfg::sched::MAX_TASKS): no heap growth after boot.
//  - millis()-rollover safe (unsigned subtraction).
//  - A late task runs once and reschedules from "now" — no catch-up bursts.
//  - Interval 0 means "run every loop pass".
//  - Every run is timed; tasks exceeding cfg::sched::TASK_BUDGET_WARN_MS are
//    logged, which is how blocking regressions get caught early.
class TaskScheduler {
 public:
  using TaskFn = std::function<void()>;
  static const int INVALID_ID = -1;

  // `name` must outlive the scheduler (use string literals).
  int add(const char* name, uint32_t intervalMs, TaskFn fn, bool enabled = true);
  void setInterval(int id, uint32_t intervalMs);
  void setEnabled(int id, bool enabled);
  void requestRun(int id);  // run at the next loop pass, regardless of interval
  void loop();
  uint8_t count() const;

 private:
  struct Task {
    const char* name = nullptr;
    TaskFn fn;
    uint32_t intervalMs = 0;
    uint32_t lastRunMs = 0;
    uint32_t lastDurationMs = 0;
    uint32_t maxDurationMs = 0;
    bool enabled = false;
    bool runRequested = false;
    bool used = false;
  };

  bool validId(int id) const;

  Task _tasks[cfg::sched::MAX_TASKS];
};
