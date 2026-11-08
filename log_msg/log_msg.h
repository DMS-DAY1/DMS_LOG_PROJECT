#pragma once

#include <string_view>

#include <string>

#define LOGGER_LEVEL_TRACE 0
#define LOGGER_LEVEL_DEBUG 1
#define LOGGER_LEVEL_INFO 2
#define LOGGER_LEVEL_WARN 3
#define LOGGER_LEVEL_ERROR 4
#define LOGGER_LEVEL_CRITICAL 5
#define LOGGER_LEVEL_OFF 6

#ifndef LOGGER_ACTIVE_LEVEL
#define LOGGER_ACTIVE_LEVEL LOGGER_LEVEL_TRACE
#endif


namespace my_log {

    using StringView = std::string_view;

    enum class Log_Level {
        kTrace = LOGGER_LEVEL_TRACE,
        kDebug = LOGGER_LEVEL_DEBUG,
        kInfo = LOGGER_LEVEL_INFO,
        kWarn = LOGGER_LEVEL_WARN,
        kError = LOGGER_LEVEL_ERROR,
        kFatal = LOGGER_LEVEL_CRITICAL,
        kOff = LOGGER_LEVEL_OFF
    };
    struct SourceLocation {
        constexpr SourceLocation() = default;

        SourceLocation(StringView file_name_, StringView func_name_, int32_t line_) : file_name(file_name_), 
        func_name(func_name_), line(line_) {
            if (!file_name.empty()) {
                size_t pos = file_name.rfind("/");
                if (pos != StringView::npos) {
                    file_name = file_name.substr(pos + 1);
                } else {
                    pos = file_name.rfind("\\");
                    if (pos != StringView::npos) {
                        file_name = file_name.substr(pos + 1);
                    }
                }
            }
        }

        StringView file_name;
        
        StringView func_name;

        int32_t line{0};
    };

    struct LogMsg {
        LogMsg(SourceLocation loc, Log_Level l, StringView msg) :location(loc), level(l), message(msg)  {}
        LogMsg(Log_Level l , StringView msg) : level(l), message(msg) {}
        
        LogMsg(const LogMsg&) = default;
        LogMsg& operator=(const LogMsg&) = default;

        SourceLocation location;

        Log_Level level;

        StringView message;
    };

} // namespace my_log
