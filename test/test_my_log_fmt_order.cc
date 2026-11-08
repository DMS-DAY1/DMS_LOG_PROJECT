// 故意在任何项目头文件之前先展开 fmt/format.h。
//
// fmt/format.h 由 FMT_FORMAT_H_ 守住，一个翻译单元里内容只会展开一次；
// 它在这次展开时会读 FMT_HEADER_ONLY，决定要不要把 format-inl.h 里的实现体拉进来。
// 因此这个宏必须由 CMake 的 add_compile_definitions(FMT_HEADER_ONLY=) 在编译器启动时给出，
// 光靠 my_log.h 内部的 #ifndef 保护是来不及的。
//
// 回归价值：一旦那条全局编译定义被删掉，这个 TU 会立刻 LNK2019
// （report_error / vformat_to / allocate 无定义），构建期就暴露，不用等运行时。

#include "fmt/format.h"

#include "my_log/my_log.h"

int FmtOrderProbe() {
    // 用底层 LOGGER_CALL 而不是 EXT_LOG_INFO：后者会被 LOGGER_ACTIVE_LEVEL 关成 (void)0，
    // 那样本 TU 就不再实例化 fmt，守门作用随编译期开关一起消失。
    // 没装 handle 时 if 判空会跳过，但模板实例化仍会被编译、被链接，
    // 所以哪怕运行时不执行，也能守住 header-only 的符号。
    LOGGER_CALL(my_log::LogFactory::GetInstance().GetLogHandle(),
                my_log::LogLevel::kInfo, "fmt order {}", 1);
    return 7;
}
