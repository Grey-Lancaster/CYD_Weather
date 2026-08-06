#include "storage/SdCardManager.h"

#include <SD.h>
#include <SPI.h>

#include "config/AppConfig.h"
#include "utils/Log.h"

static const char* TAG = "SD";

bool SdCardManager::withCard(std::function<bool()> work) {
  SPIClass sdSpi(VSPI);
  sdSpi.begin(cfg::pins::SD_SCK, cfg::pins::SD_MISO, cfg::pins::SD_MOSI, cfg::pins::SD_CS);
  const bool mounted = SD.begin(cfg::pins::SD_CS, sdSpi, cfg::sdcard::SPI_HZ);
  bool ok = false;
  if (mounted) {
    ok = work();
    SD.end();
  } else {
    LOGW(TAG, "no card detected");
  }
  sdSpi.end();
  // Hand the shared VSPI peripheral straight back to touch — see the class
  // comment on why this can't be skipped or deferred.
  if (_onBusReleased) _onBusReleased();
  return ok;
}

bool SdCardManager::isPresent() {
  return withCard([]() { return true; });
}

bool SdCardManager::writeFile(const char* path, const String& content) {
  return withCard([path, &content]() {
    File f = SD.open(path, "w");
    if (!f) {
      LOGE(TAG, "open for write failed: %s", path);
      return false;
    }
    const size_t written = f.print(content);
    f.close();
    if (written != content.length()) {
      LOGE(TAG, "short write to %s (%u of %u bytes)", path, (unsigned)written,
           (unsigned)content.length());
      return false;
    }
    return true;
  });
}

bool SdCardManager::readFile(const char* path, String& outContent) {
  return withCard([path, &outContent]() {
    if (!SD.exists(path)) {
      LOGW(TAG, "%s not found on card", path);
      return false;
    }
    File f = SD.open(path, "r");
    if (!f) {
      LOGE(TAG, "open for read failed: %s", path);
      return false;
    }
    outContent = f.readString();
    f.close();
    return true;
  });
}
