#pragma once

#include <memory>

namespace my_log {
    class LogSink;
    using LogSinkPtr = std::shared_ptr<LogSink>;
    using LogSinkPtrList = std::initializer_list<LogSinkPtr>;

    class LogHandle {
    public:
        explicit LogHandle(LogSinkPtrList sinks);

        explicit LogHandle(LogSinkPtr sink);

        template <typename It>
        LogHandle(It begin, It end) : LogHandle(LogHandle)
    };
}