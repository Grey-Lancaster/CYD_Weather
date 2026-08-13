#pragma once

// Semantic firmware version — bump on every release.
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 4
#define FW_VERSION_PATCH 5

#define FW_STRINGIFY2(x) #x
#define FW_STRINGIFY(x) FW_STRINGIFY2(x)

#define FW_VERSION \
  FW_STRINGIFY(FW_VERSION_MAJOR) "." FW_STRINGIFY(FW_VERSION_MINOR) "." FW_STRINGIFY(FW_VERSION_PATCH)

#define FW_NAME "CYD Weather"
#define FW_BUILD_DATE __DATE__ " " __TIME__
