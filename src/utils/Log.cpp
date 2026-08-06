#include "utils/Log.h"

#include "config/AppConfig.h"

LogLevel Log::_level = static_cast<LogLevel>(cfg::log::DEFAULT_LEVEL);

void Log::begin(uint32_t baud) {
  Serial.begin(baud);
}

void Log::setLevel(LogLevel level) {
  if (level == _level) return;
  _level = level;
  info("LOG", "level set to %s", levelName(level));
}

const char* Log::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::None: return "None";
    case LogLevel::Error: return "Error";
    case LogLevel::Warn: return "Warn";
    case LogLevel::Info: return "Info";
    case LogLevel::Debug: return "Debug";
  }
  return "?";
}

void Log::write(LogLevel lvl, const char* tag, const char* fmt, va_list args) {
  if (_level == LogLevel::None || static_cast<uint8_t>(lvl) > static_cast<uint8_t>(_level)) return;
  static const char kLevelChar[] = {'-', 'E', 'W', 'I', 'D'};
  char buf[cfg::log::LINE_BUFFER];
  vsnprintf(buf, sizeof(buf), fmt, args);
  Serial.printf("[%10lu][%c][%-5s] %s\r\n", (unsigned long)millis(),
                kLevelChar[static_cast<uint8_t>(lvl)], tag, buf);
}

void Log::error(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write(LogLevel::Error, tag, fmt, args);
  va_end(args);
}

void Log::warn(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write(LogLevel::Warn, tag, fmt, args);
  va_end(args);
}

void Log::info(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write(LogLevel::Info, tag, fmt, args);
  va_end(args);
}

void Log::debug(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write(LogLevel::Debug, tag, fmt, args);
  va_end(args);
}
