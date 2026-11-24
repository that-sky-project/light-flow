#ifndef __UTILS_LOGGER_HPP__
#define __UTILS_LOGGER_HPP__

#include <fstream>
#include <mutex>
#include "utils/types.hpp"

#define LOG(msg, ...) GetLogger()->Log(Logger::kLevel_Info, nullptr, (msg), ## __VA_ARGS__)
#define LOGI(msg, ...) GetLogger()->Log(Logger::kLevel_Verbose, nullptr, (msg), ## __VA_ARGS__)
#define LOGW(msg, ...) GetLogger()->Log(Logger::kLevel_Warning, nullptr, (msg), ## __VA_ARGS__)
#define LOGE(msg, ...) GetLogger()->Log(Logger::kLevel_Error, nullptr, (msg), ## __VA_ARGS__)

class Logger {
public:
  enum Level: i32 {
    kLevel_Verbose = 0,
    kLevel_Info,
    kLevel_Warning,
    kLevel_Error
  };

public:
  Logger() = default;
  ~Logger() = default;

  void Initialize(const std::wstring &path);
  void Terminate();

  inline void SetLogLevel(i32 level) { m_level = level; }

  void Log(i32 level, cstring tag, cstring format, ...);
  void Logv(i32 level, cstring tag, cstring format, va_list va);

private:
  i32 m_level = kLevel_Info;
  std::wstring m_path = L"";
  std::ofstream m_fstream = {};
  std::mutex m_mutex = {};
};

Logger *GetLogger();

#endif
