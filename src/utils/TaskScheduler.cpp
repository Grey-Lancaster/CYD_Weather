#include "utils/TaskScheduler.h"

#include "utils/Log.h"

static const char* TAG = "SCHED";

int TaskScheduler::add(const char* name, uint32_t intervalMs, TaskFn fn, bool enabled) {
  if (fn == nullptr || name == nullptr) return INVALID_ID;
  for (uint8_t i = 0; i < cfg::sched::MAX_TASKS; i++) {
    Task& t = _tasks[i];
    if (t.used) continue;
    t.used = true;
    t.name = name;
    t.fn = fn;
    t.intervalMs = intervalMs;
    t.lastRunMs = millis();
    t.lastDurationMs = 0;
    t.maxDurationMs = 0;
    t.enabled = enabled;
    t.runRequested = false;
    LOGD(TAG, "task %d '%s' every %lu ms", (int)i, name, (unsigned long)intervalMs);
    return i;
  }
  LOGE(TAG, "task table full (%u), cannot add '%s'", (unsigned)cfg::sched::MAX_TASKS, name);
  return INVALID_ID;
}

bool TaskScheduler::validId(int id) const {
  return id >= 0 && id < (int)cfg::sched::MAX_TASKS && _tasks[id].used;
}

void TaskScheduler::setInterval(int id, uint32_t intervalMs) {
  if (validId(id)) _tasks[id].intervalMs = intervalMs;
}

void TaskScheduler::setEnabled(int id, bool enabled) {
  if (validId(id)) _tasks[id].enabled = enabled;
}

void TaskScheduler::requestRun(int id) {
  if (validId(id)) _tasks[id].runRequested = true;
}

uint8_t TaskScheduler::count() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < cfg::sched::MAX_TASKS; i++) {
    if (_tasks[i].used) n++;
  }
  return n;
}

void TaskScheduler::loop() {
  for (uint8_t i = 0; i < cfg::sched::MAX_TASKS; i++) {
    Task& t = _tasks[i];
    if (!t.used || !t.enabled) continue;
    const uint32_t now = millis();
    if (!t.runRequested && (now - t.lastRunMs) < t.intervalMs) continue;
    t.runRequested = false;
    t.lastRunMs = now;
    t.fn();
    const uint32_t duration = millis() - now;
    t.lastDurationMs = duration;
    if (duration > t.maxDurationMs) t.maxDurationMs = duration;
    if (duration > cfg::sched::TASK_BUDGET_WARN_MS) {
      LOGW(TAG, "task '%s' ran for %lu ms (budget %lu ms)", t.name,
           (unsigned long)duration, (unsigned long)cfg::sched::TASK_BUDGET_WARN_MS);
    }
  }
}
