#pragma once

#include "loghandle.h"

#include "fmt/format.h"

namespace my_log {
    class VariabledLogHandle : public LogHandle {
    public:
        using LogHandle::LogHandle;
        template <typename... Args>
        void Log(SourceLocation loc, Log_Level lvl, fmt::format_string<Args...> fmt,Args&&... args) {
            Log_(loc, lvl, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void Log(Log_Level lvl, fmt::format_string<Args...> fmt,Args&&... args) {
            Log(SourceLocation(), lvl, fmt, std::forward<Args>(args)...);
        }
    private:
        template <typename... Args>
        void Log_(SourceLocation loc, Log_Level lvl, fmt::string_view fmt, Args&&... args) {
            if (!ShouldLog_(lvl)) {
                return;
            }
            fmt::basic_memory_buffer<char, 256> buf;
            fmt::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
            LogMsg msg(loc, lvl, StringView(buf.data(), buf.size()));
            LogHandle::Log_(msg);
        }
    };

}