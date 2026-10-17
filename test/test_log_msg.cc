#include "log_msg/log_msg.h"

#include <iostream>

int main() {
    my_log::StringView filename = "ab/bb/cc.cpp";
    my_log::StringView funcname = "add";
    int32_t line = 33;
    my_log::SourceLocation s(filename, funcname, line);
    my_log::Log_Level l = my_log::Log_Level::Off;
    my_log::StringView msg = "log message";
    my_log::LogMsg mm(s, l , msg);
    std::cout << s.file_name << s.func_name << s.line << std::endl;
    std::cout << mm.message << static_cast<int>(mm.level);

}
