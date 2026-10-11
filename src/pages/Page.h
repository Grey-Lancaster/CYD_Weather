#pragma once

#include <Arduino.h>

// Base class for full-screen pages.
//
// Rendering contract:
//  - render(true)  : the screen was just cleared — repaint everything.
//  - render(false) : repaint only dynamic regions.
//  - PageManager calls render() only when the page is dirty (or on a page
//    switch); pages mark themselves dirty from tick() when data changed.
//
// Input contract:
//  - handleTap returns true when the tap was consumed; an unconsumed tap
//    is ignored: pages change by swiping (see main.cpp).
class Page {
 public:
  virtual ~Page() {}

  virtual const char* name() const = 0;
  virtual void onEnter() { _dirty = true; }
  virtual void onExit() {}
  virtual void render(bool full) = 0;
  virtual bool handleTap(int16_t, int16_t) { return false; }
  virtual void tick(uint32_t) {}

  bool isDirty() const { return _dirty; }
  void requestRedraw() { _dirty = true; }
  void clearDirty() { _dirty = false; }

 protected:
  bool _dirty = true;
};
