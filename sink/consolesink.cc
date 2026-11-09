#include "consolesink.h"

#include "formatter/defaultformatter.h"

my_log::ConsoleSink::ConsoleSink() : formatter_(std::make_unique<DefaultFormatter>()) {}

void my_log::ConsoleSink::Log(const my_log::LogMsg& msg) {
    MemoryBuf buf;
    formatter_->Format(msg, &buf);
    fwrite(buf.data(), 1, buf.size(), stdout);
    fwrite("\n", 1, 1, stdout);
}

void my_log::ConsoleSink::SetFormatter(std::unique_ptr<Formatter> formatter) {
    formatter_ = std::move(formatter);
}