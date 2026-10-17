#pragma once

#include "log_msg/log_msg.h"

#include <memory>

#include "formatter/formatter.h"

namespace my_log {
    class LogSink {
    public:
        virtual ~LogSink() = default;

        virtual void Log(const LogMsg& msg) = 0;

        virtual void SetFormatter(std::unique_ptr<Formatter> formatter) = 0;

        virtual void Flush() {}
    };
}