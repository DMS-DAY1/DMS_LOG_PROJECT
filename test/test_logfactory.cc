// FMT_HEADER_ONLY 必须在任何 fmt 头文件被 include 之前定义。
// 原因：fmt/format.h 末尾有 extern template 声明（显式实例化声明），它会“嘱咐”
// 编译器不要在本翻译单元实例化 decimal_point_impl 等函数，符号留给 fmt 的 .cc；
// 但这个项目只带了 fmt 的头文件（没有 format.cc），所以必须走 header-only 路线，
// 让 format-inl.h 里的定义正常实例化。
#define FMT_HEADER_ONLY

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "fmt/format-inl.h"

#include "logfactory/logfactory.h"
#include "loghandle/variabledloghandle.h"
#include "log_msg/log_msg.h"
#include "sink/sink.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        std::cout << "    [PASS] " << what << std::endl;
    } else {
        ++g_fail;
        std::cout << "    [FAIL] " << what << std::endl;
    }
}

// 内存 sink：把每条日志拷贝成 std::string 存下来。
// 注意必须拷贝——LogMsg::message 是 StringView，指向格式化 buffer 的栈内存，
// 测试里拷贝一份既验证内容又避免悬空。
class MockSink : public my_log::LogSink {
public:
    std::vector<std::string> lines;
    std::vector<my_log::Log_Level> levels;

    void Log(const my_log::LogMsg& msg) override {
        lines.emplace_back(msg.message.data(), msg.message.size());
        levels.push_back(msg.level);
    }

    void SetFormatter(std::unique_ptr<my_log::Formatter>) override {}

    // 安全读取：越界或空容器返回占位串，避免断言实参里直接 [] / back() 触发 UB
    std::string At(std::size_t i) const {
        return i < lines.size() ? lines[i] : std::string("<无记录>");
    }
    std::string Last() const { return At(lines.empty() ? 0 : lines.size() - 1); }
};

std::shared_ptr<MockSink> MakeHandle(std::shared_ptr<my_log::VariabledLogHandle>* out) {
    auto sink = std::make_shared<MockSink>();
    *out = std::make_shared<my_log::VariabledLogHandle>(sink);
    return sink;
}

// 用例 1：VariabledLogHandle 能正常构造（基类没有默认构造，
// 派生类必须 using LogHandle::LogHandle 继承过来才能 make_shared）
void CaseConstruct() {
    std::cout << "[1] VariabledLogHandle 构造（继承基类构造函数）" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);
    Check(handle != nullptr, "make_shared<VariabledLogHandle>(sink) 成功");
    Check(sink->lines.empty(), "构造后还没有任何日志");
}

// 用例 2：两参数 Log——级别 + 格式串，内容被 fmt 渲染
void CaseLogTwoArgs() {
    std::cout << "[2] Log(level, fmt, args...) 基本格式化" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);

    handle->Log(my_log::Log_Level::kInfo, "hello {}", 42);
    Check(sink->lines.size() == 1, "产生 1 条日志");
    Check(sink->At(0) == "hello 42", "内容精确等于 hello 42（实际: " + sink->Last() + "）");
    Check(sink->levels.size() == 1 && sink->levels[0] == my_log::Log_Level::kInfo, "级别记录为 Info");
}

// 用例 3：三参数 Log——带 SourceLocation，内容同样被渲染
void CaseLogWithLocation() {
    std::cout << "[3] Log(loc, level, fmt, args...) 带源码位置" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);

    handle->Log(my_log::SourceLocation("a/b/x.cc", "f", 7), my_log::Log_Level::kWarn, "v={}", 1.5);
    Check(sink->lines.size() == 1, "产生 1 条日志");
    Check(sink->At(0) == "v=1.5", "浮点参数渲染正确（实际: " + sink->Last() + "）");
    Check(sink->levels.size() == 1 && sink->levels[0] == my_log::Log_Level::kWarn, "级别记录为 Warn");
}

// 用例 4：多个参数 + 混合类型（级别须达到默认阈值 kInfo，否则日志会被过滤）
void CaseLogMultiArgs() {
    std::cout << "[4] 多参数混合类型格式化" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);

    const std::string name = "dms";
    handle->Log(my_log::Log_Level::kInfo, "{} {} {} {}", 1, name, 2.5f, 'c');
    Check(sink->lines.size() == 1, "产生 1 条日志");
    Check(sink->At(0) == "1 dms 2.5 c", "int/string/float/char 混排（实际: " + sink->Last() + "）");
}

// 用例 5：无占位符的纯文本
void CaseLogPlainText() {
    std::cout << "[5] 无占位符纯文本" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);

    handle->Log(my_log::Log_Level::kError, "boom");
    Check(sink->At(0) == "boom", "纯文本原样输出（实际: " + sink->Last() + "）");
}

// 用例 6：SetLevel 级别过滤（基类 ShouldLog_ 逻辑在派生类里同样生效）
void CaseLevelFilter() {
    std::cout << "[6] SetLevel 级别过滤" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);

    handle->SetLevel(my_log::Log_Level::kWarn);
    handle->Log(my_log::Log_Level::kInfo, "info {}", 1);   // 应被过滤
    handle->Log(my_log::Log_Level::kDebug, "dbg");         // 应被过滤
    handle->Log(my_log::Log_Level::kWarn, "warn {}", 2);   // 应通过
    handle->Log(my_log::Log_Level::kError, "err");         // 应通过
    Check(sink->lines.size() == 2, "4 投 2 收，Info/Debug 被过滤");
    Check(sink->At(0) == "warn 2" && sink->At(1) == "err", "留下的是 Warn 和 Error");
}

// 用例 7：LogFactory 单例唯一性
void CaseFactorySingleton() {
    std::cout << "[7] LogFactory 单例" << std::endl;
    my_log::LogFactory& a = my_log::LogFactory::GetInstance();
    my_log::LogFactory& b = my_log::LogFactory::GetInstance();
    Check(&a == &b, "两次 GetInstance 拿到同一个对象");
}

// 用例 8：LogFactory Set/Get 往返 + 默认 nullptr + 可替换
void CaseFactorySetGet() {
    std::cout << "[8] LogFactory SetLogHandle / GetLogHandle" << std::endl;
    my_log::LogFactory& factory = my_log::LogFactory::GetInstance();
    Check(factory.GetLogHandle() == nullptr, "初始状态 GetLogHandle 为空");

    std::shared_ptr<my_log::VariabledLogHandle> h1;
    auto s1 = MakeHandle(&h1);
    factory.SetLogHandle(h1);
    Check(factory.GetLogHandle() == h1.get(), "设置后 GetLogHandle 返回同一对象");

    std::shared_ptr<my_log::VariabledLogHandle> h2;
    auto s2 = MakeHandle(&h2);
    factory.SetLogHandle(h2);
    Check(factory.GetLogHandle() == h2.get(), "替换后拿到的是新对象");

    // 还原，避免影响其他用例
    factory.SetLogHandle(nullptr);
    Check(factory.GetLogHandle() == nullptr, "置空后恢复初始状态");
}

// 用例 9：factory 与 handle 配合——从 factory 拿 handle 直接打日志
void CaseFactoryUsage() {
    std::cout << "[9] 从 factory 取 handle 打日志" << std::endl;
    std::shared_ptr<my_log::VariabledLogHandle> handle;
    auto sink = MakeHandle(&handle);
    my_log::LogFactory::GetInstance().SetLogHandle(handle);

    my_log::LogFactory::GetInstance().GetLogHandle()->Log(my_log::Log_Level::kInfo, "via factory {}", 7);
    Check(sink->lines.size() == 1 && sink->At(0) == "via factory 7",
          "factory 路径打通（实际: " + sink->Last() + "）");

    my_log::LogFactory::GetInstance().SetLogHandle(nullptr);
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "===== test_logfactory：LogFactory + VariabledLogHandle =====" << std::endl;

    CaseConstruct();
    CaseLogTwoArgs();
    CaseLogWithLocation();
    CaseLogMultiArgs();
    CaseLogPlainText();
    CaseLevelFilter();
    CaseFactorySingleton();
    CaseFactorySetGet();
    CaseFactoryUsage();

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "通过 " << g_pass << " 项，失败 " << g_fail << " 项" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
