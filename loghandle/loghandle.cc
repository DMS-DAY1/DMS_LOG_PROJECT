#include "loghandle.h"

#include "sink/sink.h"

my_log::LogHandle::LogHandle(LogSinkPtrList sinks) : level_{my_log::Log_Level::kInfo} {
    for (auto& it : sinks) {
        sinks_.push_back(std::move(it));
    }
}

my_log::LogHandle::LogHandle(LogSinkPtr sink) : level_{LogLevel::kInfo} {
    sinks_.push_back(std::move(sink));
}

void my_log::LogHandle::SetLevel(my_log::Log_Level level) {
    level_ = level;
}

    my_log::LogLevel my_log::LogHandle::GetLevel() const {
        return level_;
    }

void my_log::LogHandle::Log(my_log::LogLevel level, my_log::SourceLocation loc, my_log::StringView message) {
    if (!ShouldLog_(level)) {
        return;
    }
    my_log::LogMsg msg(loc, level, message);
    Log_(msg);
}

 void my_log::LogHandle::Log_(const my_log::LogMsg& log_msg) {
    for (auto& sink : sinks_) {
        sink->Log(log_msg);
    }
 }