#pragma once

#include <Arduino.h>
#include <stdarg.h>

enum class LogLevel : uint8_t { None = 0, Error, Warn, Info, Debug };

// Leveled, tagged serial logger. Level is runtime-adjustable (persisted in
// settings, changeable live from the web portal).
//   [    12345][I][NET  ] connected, ip=192.168.1.20
class Log {
 public:
  static void begin(uint32_t baud);
  static void setLevel(LogLevel level);
  static LogLevel level() { return _level; }
  static const char* levelName(LogLevel level);

  static void error(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  static void warn(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  static void info(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  static void debug(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

 private:
  static void write(LogLevel lvl, const char* tag, const char* fmt, va_list args);
  static LogLevel _level;
};

#define LOGE(tag, ...) Log::error(tag, __VA_ARGS__)
#define LOGW(tag, ...) Log::warn(tag, __VA_ARGS__)
#define LOGI(tag, ...) Log::info(tag, __VA_ARGS__)
#define LOGD(tag, ...) Log::debug(tag, __VA_ARGS__)
