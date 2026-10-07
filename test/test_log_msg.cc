#include "log_msg/log_msg.h"

#include <iostream>

int main() {
    StringView filename = "ab/bb/cc.cpp";
    StringView funcname = "add";
    int32_t line = 33;
    SourceLocation s(filename, funcname, line);
    Log_Level l = Log_Level::Off;
    StringView msg = "log message";
    LogMsg mm(s, l , msg);
    std::cout << s.file_name << s.func_name << s.line << std::endl;
    std::cout << mm.message << static_cast<int>(mm.level);

}