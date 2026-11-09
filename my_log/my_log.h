#pragma once

#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif

#include "log_msg/log_msg.h"

#include "logfactory/logfactory.h"

#include "loghandle/loghandle.h"

#include "loghandle/variabledloghandle.h"

#include <fmt/format.h>

#define EXT_LOGGER_INIT(handle) my_log::LogFactory::GetInstance().SetLogHandle(handle)

// 只求值一次：判空与 Log 必须用同一个句柄，避免两次取句柄之间被替换
#define LOGGER_CALL(handle, level, ...)                                                                        \
  if (auto* h = (handle)) {                                                                                                \
      h->Log(my_log::SourceLocation{__FILE__, static_cast<const char*>(__FUNCTION__), __LINE__}, level,  \
                  __VA_ARGS__);                                                                                \
  }

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_TRACE
#define LOG_LOGGER_TRACE(handle, ...) LOGGER_CALL(handle, my_log::LogLevel::kTrace, __VA_ARGS__)
#define EXT_LOG_TRACE(...) LOG_LOGGER_TRACE(my_log::LogFactory::GetInstance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_TRACE(handle, ...) (void)0
#define EXT_LOG_TRACE(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_DEBUG
#define LOG_LOGGER_DEBUG(handle, ...) LOGGER_CALL(handle, my_log::LogLevel::kDebug, __VA_ARGS__)
#define EXT_LOG_DEBUG(...) LOG_LOGGER_DEBUG(my_log::LogFactory::GetInstance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_DEBUG(handle, ...) (void)0
#define EXT_LOG_DEBUG(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_INFO
#define LOG_LOGGER_INFO(handle, ...) LOGGER_CALL(handle, my_log::LogLevel::kInfo, __VA_ARGS__)
#define EXT_LOG_INFO(...) LOG_LOGGER_INFO(my_log::LogFactory::GetInstance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_INFO(handle, ...) (void)0
#define EXT_LOG_INFO(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_WARN
#define LOG_LOGGER_WARN(handle, ...) LOGGER_CALL(handle, my_log::LogLevel::kWarn, __VA_ARGS__)
#define EXT_LOG_WARN(...) LOG_LOGGER_WARN(my_log::LogFactory::GetInstance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_WARN(handle, ...) (void)0
#define EXT_LOG_WARN(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_ERROR
#define LOG_LOGGER_ERROR(handle, ...) LOGGER_CALL(handle, my_log::LogLevel::kError, __VA_ARGS__)
#define EXT_LOG_ERROR(...) LOG_LOGGER_ERROR(my_log::LogFactory::GetInstance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_ERROR(handle, ...) (void)0
#define EXT_LOG_ERROR(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_CRITICAL
#define LOG_LOGGER_CRITICAL(handle, ...) LOGGER_CALL(handle, my_log::LogLevel::kFatal, __VA_ARGS__)
#define EXT_LOG_CRITICAL(...) LOG_LOGGER_CRITICAL(my_log::LogFactory::GetInstance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_CRITICAL(handle, ...) (void)0
#define EXT_LOG_CRITICAL(...) (void)0
#endif
