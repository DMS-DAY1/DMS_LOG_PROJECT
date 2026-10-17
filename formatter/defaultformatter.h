#pragma once

#include "formatter.h"

namespace my_log {
    class DefaultFormatter : public Formatter {
    public:
        void Format(const LogMsg& msg, MemoryBuf* dest) override;
    };

}