#pragma once

#include "formatter.h"

namespace my_log {
    class EffectiveFormatter : public Formatter {
    public:
        virtual void Format(const LogMsg& msg, MemoryBuf* dest);
    }; 
}