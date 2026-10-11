#include "pages/PageManager.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "utils/Log.h"

static const char* TAG = "UI";

namespace {
uint8_t lerpU8(uint8_t from, uint8_t to, uint32_t elapsedMs, uint32_t durationMs) {
  if (durationMs == 0 || elapsedMs >= durationMs) return to;
  const int32_t delta = (int32_t)to - (int32_t)from;
  const int32_t value = (int32_t)from + (delta * (int32_t)elapsedMs) / (int32_t)durationMs;
  if (value < 0) return 0;
  if (value > 255) return 255;
  return (uint8_t)value;
}
}  // namespace

PageManager::PageManager(DisplayManager& display, StatusBar& statusBar)
    : _display(display), _statusBar(statusBar) {}

bool PageManager::addPage(Page* page, bool rotatable) {
  if (page == nullptr) return false;
  if (_count >= cfg::ui::MAX_PAGES) {
    LOGE(TAG, "page registry full (%u), cannot add '%s'", (unsigned)cfg::ui::MAX_PAGES,
         page->name());
    return false;
  }
  _rotatable[_count] = rotatable;
  _pages[_count++] = page;
  return true;
}

void PageManager::activateImmediate(uint8_t index) {
  if (_pages[_active] != nullptr) _pages[_active]->onExit();
  _active = index;
  _pages[_active]->onEnter();
  _needsFullRedraw = true;
  _activeSinceMs = millis();
  LOGI(TAG, "page -> %s", _pages[_active]->name());
}

void PageManager::setActive(uint8_t index) {
  if (index >= _count) return;
  if (_transition != Transition::Idle) return;  // one transition at a time
  if (!_everActivated) {
    _everActivated = true;
    activateImmediate(index);
    return;
  }
  if (index == _active) return;
  beginTransition(index);
}

bool PageManager::setActive(Page* page) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_pages[i] == page) {
      setActive(i);
      return true;
    }
  }
  return false;
}

void PageManager::next() {
  if (_count < 2 || _transition != Transition::Idle) return;
  for (uint8_t step = 1; step <= _count; step++) {
    const uint8_t candidate = (uint8_t)((_active + step) % _count);
    if (_rotatable[candidate]) {
      setActive(candidate);
      return;
    }
  }
}

void PageManager::prev() {
  if (_count < 2 || _transition != Transition::Idle) return;
  for (uint8_t step = 1; step <= _count; step++) {
    const uint8_t candidate = (uint8_t)((_active + _count - step) % _count);
    if (_rotatable[candidate]) {
      setActive(candidate);
      return;
    }
  }
}

bool PageManager::rotatablePosition(uint8_t& current, uint8_t& total) const {
  total = 0;
  uint8_t currentOrdinal = 0;
  bool found = false;
  for (uint8_t i = 0; i < _count; i++) {
    if (!_rotatable[i]) continue;
    if (i == _active) {
      currentOrdinal = total;
      found = true;
    }
    total++;
  }
  if (!found) return false;
  current = currentOrdinal;
  return true;
}

void PageManager::setAutoRotate(bool enabled, uint16_t pageDurationS) {
  _autoRotateEnabled = enabled;
  _pageDurationMs = (uint32_t)pageDurationS * 1000u;
}

void PageManager::paintActivePage() {
  _display.tft().fillScreen(theme().bg);
  _statusBar.render(true);
  _pages[_active]->render(true);
  _pages[_active]->clearDirty();
  drawIndicator();
}

void PageManager::drawIndicator() {
  uint8_t current = 0, total = 0;
  if (rotatablePosition(current, total) && total > 1) {
    ui::drawPageIndicator(_display.tft(), current, total);
  }
}

void PageManager::beginTransition(uint8_t index) {
  _pendingIndex = index;
  _transition = Transition::FadingOut;
  _transitionStartMs = millis();
}

void PageManager::stepFadeOut(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - _transitionStartMs;
  if (elapsed >= cfg::ui::PAGE_TRANSITION_MS) {
    // Swap and repaint now, while the backlight is at its dimmest — the
    // redraw itself is never visible, which is what makes this flicker-free.
    _display.setBacklightRaw(cfg::display::MIN_BRIGHTNESS);
    activateImmediate(_pendingIndex);
    paintActivePage();
    _needsFullRedraw = false;
    _transition = Transition::FadingIn;
    _transitionStartMs = nowMs;
    return;
  }
  _display.setBacklightRaw(lerpU8(_display.restingBrightness(), cfg::display::MIN_BRIGHTNESS,
                                   elapsed, cfg::ui::PAGE_TRANSITION_MS));
}

void PageManager::stepFadeIn(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - _transitionStartMs;
  if (elapsed >= cfg::ui::PAGE_TRANSITION_MS) {
    _display.setBacklightRaw(_display.restingBrightness());
    _transition = Transition::Idle;
    return;
  }
  _display.setBacklightRaw(lerpU8(cfg::display::MIN_BRIGHTNESS, _display.restingBrightness(),
                                   elapsed, cfg::ui::PAGE_TRANSITION_MS));
}

void PageManager::render() {
  if (_count == 0) return;
  const uint32_t now = millis();
  if (_transition == Transition::FadingOut) {
    stepFadeOut(now);
    return;
  }
  if (_transition == Transition::FadingIn) {
    stepFadeIn(now);
    return;
  }

  Page* page = _pages[_active];
  if (_needsFullRedraw) {
    paintActivePage();
    _needsFullRedraw = false;
    return;
  }
  _statusBar.render(false);
  if (page->isDirty()) {
    page->render(false);
    page->clearDirty();
  }
}

void PageManager::tick(uint32_t nowMs) {
  if (_count == 0) return;
  _pages[_active]->tick(nowMs);
  if (_autoRotateEnabled && _rotatable[_active] && _transition == Transition::Idle &&
      nowMs - _activeSinceMs >= _pageDurationMs) {
    next();
  }
}

void PageManager::handleTap(int16_t x, int16_t y) {
  if (_count == 0) return;
  if (y < (int16_t)theme().statusBarH) return;  // status bar taps reserved
  if (_pages[_active]->handleTap(x, y)) {
    _activeSinceMs = millis();  // interacting with a page restarts its auto-rotate timer
  } else {
    next();
  }
}
