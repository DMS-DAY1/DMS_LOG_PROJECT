#include <cstdio>
#include <ctime>

#include "utils/file_util.h"
#include "utils/sys_util.h"

// 最小验证骨架：把 utils 四个平台函数 + GetFileSize 各跑一遍看输出。
// 正式测试用例你自己补，这里只证明"接进 CMake 后能编能跑"。
int main() {
    // 1. LocalTime：应打出当前日期时间（2026-10-17 xx:xx:xx）
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    my_log::LocalTime(&tm, &now);
    std::printf("LocalTime  : %04d-%02d-%02d %02d:%02d:%02d\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);

    // 2~4. 三个系统信息函数
    std::printf("PageSize   : %zu\n", my_log::GetPageSize());
    std::printf("ProcessId  : %zu\n", my_log::GetProcessId());
    std::printf("ThreadId   : %zu\n", my_log::GetThreadId());

    // 5. GetFileSize：用 __FILE__ 这个源文件自身验证，大小应是几百字节
    std::printf("FileSize   : %zu\n", my_log::fs::GetFileSize(__FILE__));

    return 0;
}
