#pragma once

#include <memory>

#include "log_msg/log_msg.h"

#include <vector>

#include <atomic>

namespace my_log {
    class LogSink;
    using LogSinkPtr = std::shared_ptr<LogSink>;
    using LogSinkPtrList = std::initializer_list<LogSinkPtr>;

    using LogLevel = Log_Level;

    class LogHandle {
    public:
        explicit LogHandle(LogSinkPtrList sinks);

        explicit LogHandle(LogSinkPtr sink);

        template <typename It>
        LogHandle(It begin, It end) {
            for (auto& it = begin; it != end; it++) {
                sinks_.push_back(std::move(*it));
            }
        }

        ~LogHandle() = default;

        LogHandle(const LogHandle&) = delete;
        LogHandle& operator=(const LogHandle&) = delete;

        void SetLevel(LogLevel level);

        LogLevel GetLevel() const;

        void Log(LogLevel level, SourceLocation loc, StringView message);
    protected:
        bool ShouldLog_(LogLevel level) const noexcept {
            return level >= level_&& ! sinks_.empty();
        }

        void Log_(const LogMsg& log_msg);
    private:
        std::atomic<LogLevel> level_;
        std::vector<LogSinkPtr> sinks_;
    };
}