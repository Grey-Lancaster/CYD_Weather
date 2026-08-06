#pragma once

#include <Arduino.h>

#include <functional>

// Wraps the board's microSD slot for the "backup settings to SD" web UI
// feature. The slot's SPI lines share the ESP32's VSPI peripheral with
// TouchManager (the TFT already owns the only other one, HSPI) — the two
// use different pin mappings (see cfg::pins::SD_* vs TOUCH_*), so whichever
// one didn't go last has its pin routing silently stolen out from under it.
// To keep that safe, this class never leaves the bus "claimed": every method
// below does its own SPIClass.begin()/SD.end()/SPIClass.end() around a single
// file operation and then, before returning, invokes the onBusReleased
// callback (wired to TouchManager::reacquireBus() in main.cpp) so touch
// polling immediately after still works. Because of that, callers must only
// use this from a context that won't be interrupted by another task running
// mid-operation — true for a synchronous web request handler on this
// project's single-threaded cooperative scheduler, not necessarily true in
// general.
class SdCardManager {
 public:
  using BusReleasedCallback = std::function<void()>;

  void onBusReleased(BusReleasedCallback cb) { _onBusReleased = cb; }

  // Mounts just long enough to confirm a card answers, then releases the bus.
  bool isPresent();

  // Writes `content` to path, overwriting any existing file.
  bool writeFile(const char* path, const String& content);

  // Reads path fully into outContent. Returns false if the card or file is
  // missing (outContent left untouched).
  bool readFile(const char* path, String& outContent);

 private:
  bool withCard(std::function<bool()> work);

  BusReleasedCallback _onBusReleased;
};
