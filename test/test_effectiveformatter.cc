// EffectiveFormatter 单元测试：验证 LogMsg -> EffectiveMsg 字节流 的序列化是否正确。
//
// 被测对象是 formatter/effectiveformatter.cc 里 DMS 写的 EffectiveFormatter::Format：
//   输入  LogMsg（level / 时间戳 / pid / tid / 源码位置 / 正文）
//   输出  一串字节（my_log::proto::EffectiveMsg 的序列化结果）
//
// 测试思路：调 Format 拿到字节流 -> 用 protobuf 反序列化回去 -> 逐字段断言，
// 确认“人填进去的”和“解出来的”完全一致。
//
// 注意：EffectiveMsg 是 LITE_RUNTIME 生成的 lite 消息（无反射），
// 所以这里用 ParseFromArray 直接反序列化，不用 TextFormat。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "formatter/effectiveformatter.h"
#include "effective_msg.pb.h"
#include "utils/sys_util.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_pass = 0;
int g_fail = 0;
std::vector<std::string> g_failed;

void Check(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what);
        g_failed.push_back(what);
    }
}

// 反序列化辅助：wire 是 Format 产出的字节流
bool Decode(const std::string& wire, my_log::proto::EffectiveMsg* out) {
    return out->ParseFromArray(wire.data(), static_cast<int>(wire.size()));
}

} // namespace

int main() {
    std::printf("==== EffectiveFormatter::Format 测试 ====\n");

    // 用例1：基本往返，逐字段核对
    {
        std::printf("[用例1] 基本字段往返\n");
        my_log::LogMsg msg(
            my_log::SourceLocation("src/logger.cc", "DoWork", 123),
            my_log::Log_Level::kInfo,
            "hello effective formatter");
        std::string wire;
        my_log::EffectiveFormatter fmt;
        fmt.Format(msg, &wire);

        my_log::proto::EffectiveMsg m;
        Check(Decode(wire, &m), "ParseFromArray 成功");
        Check(m.level() == static_cast<int32_t>(my_log::Log_Level::kInfo),
              "level == Info 的底层值");
        Check(m.timestamp() > 0, "timestamp 为正数（毫秒）");
        Check(m.pid() == static_cast<int32_t>(my_log::GetProcessId()), "pid 正确");
        Check(m.tid() == static_cast<int32_t>(my_log::GetThreadId()), "tid 正确");
        Check(m.line() == 123, "line 正确");
        Check(m.file_name() == "logger.cc", "file_name 取 basename 正确");
        Check(m.func_name() == "DoWork", "func_name 正确");
        Check(m.log_info() == "hello effective formatter", "log_info 正确");
    }

    // 用例2：每个 Log_Level 都正确映射
    {
        std::printf("[用例2] Log_Level 全枚举映射\n");
        const my_log::Log_Level levels[] = {
            my_log::Log_Level::kTrace, my_log::Log_Level::kDebug,
            my_log::Log_Level::kInfo,   my_log::Log_Level::kWarn,
            my_log::Log_Level::kError,  my_log::Log_Level::kFatal,
            my_log::Log_Level::kOff};
        my_log::EffectiveFormatter fmt;
        for (auto lv : levels) {
            my_log::LogMsg msg(my_log::SourceLocation("a.cc", "f", 1), lv, "x");
            std::string wire;
            fmt.Format(msg, &wire);
            my_log::proto::EffectiveMsg m;
            Decode(wire, &m);
            Check(m.level() == static_cast<int32_t>(lv),
                  "level 映射正确（枚举底层值）");
        }
    }

    // 用例3：同一 dest 多次 Format，后一次应完全覆盖前一次（不残留）
    {
        std::printf("[用例3] 复用 dest 的覆盖语义\n");
        my_log::EffectiveFormatter fmt;
        std::string wire;
        my_log::LogMsg m1(my_log::SourceLocation("a.cc", "f1", 1),
                          my_log::Log_Level::kDebug, "first-message");
        fmt.Format(m1, &wire);
        my_log::LogMsg m2(my_log::SourceLocation("b.cc", "f2", 2),
                          my_log::Log_Level::kError, "second-message");
        fmt.Format(m2, &wire);  // 复用同一个 wire

        my_log::proto::EffectiveMsg m;
        Check(Decode(wire, &m), "二次 Format 后仍可解析");
        Check(m.log_info() == "second-message", "只剩第二条的 log_info");
        Check(m.line() == 2, "只剩第二条的 line");
        Check(m.file_name() == "b.cc", "只剩第二条的 file_name");
    }

    // 用例4：超长正文完整往返
    {
        std::printf("[用例4] 超长正文往返\n");
        std::string big(50000, 'x');
        my_log::LogMsg msg(my_log::SourceLocation("f.cc", "g", 7),
                           my_log::Log_Level::kWarn, big);
        std::string wire;
        my_log::EffectiveFormatter fmt;
        fmt.Format(msg, &wire);
        my_log::proto::EffectiveMsg m;
        Check(Decode(wire, &m), "超长正文 ParseFromArray 成功");
        Check(m.log_info() == big, "超长正文内容一致");
        Check(m.log_info().size() == 50000, "超长正文长度一致");
    }

    // 用例5：中文 / 特殊字符（UTF-8 源码，需 /utf-8 编译）
    {
        std::printf("[用例5] 中文内容往返\n");
        my_log::LogMsg msg(my_log::SourceLocation("中文路径.cc", "处理函数", 9),
                           my_log::Log_Level::kFatal, "错误信息：连接数据库失败");
        std::string wire;
        my_log::EffectiveFormatter fmt;
        fmt.Format(msg, &wire);
        my_log::proto::EffectiveMsg m;
        Check(Decode(wire, &m), "中文内容 ParseFromArray 成功");
        Check(m.log_info() == "错误信息：连接数据库失败", "中文 log_info 正确");
        Check(m.file_name() == "中文路径.cc", "中文 file_name basename 正确");
        Check(m.func_name() == "处理函数", "中文 func_name 正确");
    }

    std::printf("\n==== 汇总 ====\n");
    std::printf("PASS=%d  FAIL=%d\n", g_pass, g_fail);
    if (!g_failed.empty()) {
        std::printf("失败项:\n");
        for (const auto& s : g_failed) std::printf("  - %s\n", s.c_str());
    }
    return g_fail == 0 ? 0 : 1;
}
