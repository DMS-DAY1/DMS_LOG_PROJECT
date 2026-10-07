#pragma once

#include <string_view>

#include <string>

using StringView = std::string_view;

enum class Log_Level {
    Trance,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
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
