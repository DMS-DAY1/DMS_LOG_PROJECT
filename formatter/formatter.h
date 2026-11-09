#pragma once

#include "log_msg/log_msg.h"

namespace my_log{
    using MemoryBuf = std::string;
    class Formatter {
    public:
        virtual ~Formatter() = default;

        virtual void Format(const LogMsg& msg, MemoryBuf* dest) = 0;
    };
}