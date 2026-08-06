#pragma once

#include "config/AppConfig.h"
#include "display/DisplayManager.h"
#include "pages/Page.h"
#include "ui/StatusBar.h"

// Owns the page registry (fixed capacity, pages are externally owned),
// drives the render pipeline and routes touch input.
class PageManager {
 public:
  PageManager(DisplayManager& display, StatusBar& statusBar);

  // `rotatable` marks whether next()/prev() (tap/swipe-advance and
  // auto-rotation) may land on this page. Pages only reachable via an
  // explicit state change (e.g. SetupPage, shown while the network is in AP
  // mode) should pass false.
  bool addPage(Page* page, bool rotatable = true);
  void setActive(uint8_t index);
  bool setActive(Page* page);  // by identity; false if not registered
  void next();
  void prev();
  Page* activePage() const { return _count > 0 ? _pages[_active] : nullptr; }
  uint8_t pageCount() const { return _count; }
  uint8_t activeIndex() const { return _active; }
  // 0-based position and total count among *rotatable* pages only — the page
  // dots. Returns false if the active page isn't rotatable (e.g. SetupPage),
  // in which case no indicator should be drawn.
  bool rotatablePosition(uint8_t& current, uint8_t& total) const;

  // Cycle through rotatable pages automatically while idle on one of them.
  void setAutoRotate(bool enabled, uint16_t pageDurationS);
  void requestFullRedraw() { _needsFullRedraw = true; }

  void render();                          // ui.render task
  void tick(uint32_t nowMs);              // ui.tick task
  void handleTap(int16_t x, int16_t y);   // from TouchManager

 private:
  enum class Transition : uint8_t { Idle, FadingOut, FadingIn };

  void activateImmediate(uint8_t index);
  void beginTransition(uint8_t index);
  void stepFadeOut(uint32_t nowMs);
  void stepFadeIn(uint32_t nowMs);
  void paintActivePage();
  void drawIndicator();

  DisplayManager& _display;
  StatusBar& _statusBar;
  Page* _pages[cfg::ui::MAX_PAGES] = {nullptr};
  bool _rotatable[cfg::ui::MAX_PAGES] = {false};
  uint8_t _count = 0;
  uint8_t _active = 0;
  bool _everActivated = false;
  bool _needsFullRedraw = true;
  bool _autoRotateEnabled = false;
  uint32_t _pageDurationMs = 0;
  uint32_t _activeSinceMs = 0;

  // Page-switch fade (see cfg::ui::PAGE_TRANSITION_MS): the backlight ramps
  // down, the new page paints while invisible, then the backlight ramps back
  // up — a flicker-free "fade" transition that needs no extra frame buffer.
  Transition _transition = Transition::Idle;
  uint8_t _pendingIndex = 0;
  uint32_t _transitionStartMs = 0;
};
