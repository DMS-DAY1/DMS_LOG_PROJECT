#pragma once

#include "log_msg/log_msg.h"

#include <memory>

#include "formatter/formatter.h"

#include "sink.h"

namespace my_log {
    
    class ConsoleSink : public LogSink{
    public:
        ConsoleSink();

        ~ConsoleSink() override = default;

        void Log(const LogMsg& msg) override;

        void SetFormatter(std::unique_ptr<Formatter> formatter) override;

        private:
            std::unique_ptr<Formatter> formatter_;
    };

}