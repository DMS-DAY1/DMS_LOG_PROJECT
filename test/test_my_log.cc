// test_my_log：对外头文件 my_log/my_log.h 的自动化回归。
//
// 断言一律跟着当前实现走：
//   - ShouldLog_ 是 level >= level_ && !sinks_.empty()；两个构造函数默认阈值 kInfo。
//   - SourceLocation 构造会把 __FILE__ 截成文件名，func_name / line 来自宏调用点。
//   - EXT_LOG_CRITICAL 映射到 Log_Level::kFatal，不是 kCritical。
//   - LOGGER_ACTIVE_LEVEL 关闭某个级别时，该宏整体展开成 (void)0，参数文本被吞。
// 因此每个用例都用 constexpr 开关算期望值，外部用 -D 改编译期级别也能全绿。
//
// 这里不手写 #define FMT_HEADER_ONLY：故意让 my_log.h 自己的 #ifndef 保护
// 和 CMake 的 add_compile_definitions 兜底，这样测的才是"使用方直接 include 对外头"的真实路径。
//
// 安全约定：绝不在 Check 的参数里直接取 records[0] / back()。日志一旦被级别过滤，
// 空容器上取下标就会崩（test_logfactory 用例 4 的教训），统一走 RecAt 安全读。

#include <cstddef>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "my_log/my_log.h"
#include "sink/sink.h"

#ifdef _WIN32
#include <windows.h>
#endif

// 定义在 test_my_log_fmt_order.cc：故意先 include fmt/format.h 再 include my_log.h
int FmtOrderProbe();

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

struct Record {
    std::string text;
    my_log::Log_Level level;
    std::string file;
    std::string func;
    int line;
};

// 内存 sink：必须把 message / location 拷成 std::string。
// LogMsg 里的字段都是 StringView，指向格式化 buffer 与 __FILE__/__FUNCTION__ 的静态存储，
// 测试里拷一份既验证内容，也避免把临时视图留到以后用。
class RecordingSink : public my_log::LogSink {
public:
    std::vector<Record> records;

    void Log(const my_log::LogMsg& msg) override {
        Record r;
        if (!msg.message.empty()) {
            r.text.assign(msg.message.data(), msg.message.size());
        }
        r.level = msg.level;
        if (!msg.location.file_name.empty()) {
            r.file.assign(msg.location.file_name.data(), msg.location.file_name.size());
        }
        if (!msg.location.func_name.empty()) {
            r.func.assign(msg.location.func_name.data(), msg.location.func_name.size());
        }
        r.line = msg.location.line;
        records.push_back(std::move(r));
    }

    void SetFormatter(std::unique_ptr<my_log::Formatter>) override {}
};

const Record& RecAt(const RecordingSink& s, std::size_t i) {
    static const Record kEmpty{"<无记录>", my_log::Log_Level::kOff, "", "", -1};
    return i < s.records.size() ? s.records[i] : kEmpty;
}

struct Fixture {
    std::shared_ptr<RecordingSink> sink;
    std::shared_ptr<my_log::VariabledLogHandle> handle;

    Fixture() {
        sink = std::make_shared<RecordingSink>();
        handle = std::make_shared<my_log::VariabledLogHandle>(sink);
        EXT_LOGGER_INIT(handle);
    }

    ~Fixture() {
        my_log::LogFactory::GetInstance().SetLogHandle(nullptr);
    }
};

constexpr bool kOnTrace = LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_TRACE;
constexpr bool kOnDebug = LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_DEBUG;
constexpr bool kOnInfo = LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_INFO;
constexpr bool kOnWarn = LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_WARN;
constexpr bool kOnError = LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_ERROR;
constexpr bool kOnCrit = LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_CRITICAL;

void CaseHeaderConstants() {
    std::cout << "[1] 对外头的级别常量与枚举对齐" << std::endl;
    Check(static_cast<int>(my_log::Log_Level::kTrace) == LOGGER_LEVEL_TRACE, "kTrace == LOGGER_LEVEL_TRACE");
    Check(static_cast<int>(my_log::Log_Level::kDebug) == LOGGER_LEVEL_DEBUG, "kDebug == LOGGER_LEVEL_DEBUG");
    Check(static_cast<int>(my_log::Log_Level::kInfo) == LOGGER_LEVEL_INFO, "kInfo == LOGGER_LEVEL_INFO");
    Check(static_cast<int>(my_log::Log_Level::kWarn) == LOGGER_LEVEL_WARN, "kWarn == LOGGER_LEVEL_WARN");
    Check(static_cast<int>(my_log::Log_Level::kError) == LOGGER_LEVEL_ERROR, "kError == LOGGER_LEVEL_ERROR");
    Check(static_cast<int>(my_log::Log_Level::kFatal) == LOGGER_LEVEL_CRITICAL, "kFatal == LOGGER_LEVEL_CRITICAL");
    Check(static_cast<int>(my_log::Log_Level::kOff) == LOGGER_LEVEL_OFF, "kOff == LOGGER_LEVEL_OFF");
    Check(LOGGER_ACTIVE_LEVEL >= LOGGER_LEVEL_TRACE && LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_OFF,
          "LOGGER_ACTIVE_LEVEL 落在合法区间");
    std::cout << "    (当前 LOGGER_ACTIVE_LEVEL = " << LOGGER_ACTIVE_LEVEL << ")" << std::endl;
}

void CaseInit() {
    std::cout << "[2] EXT_LOGGER_INIT 安装句柄" << std::endl;
    Fixture f;
    Check(my_log::LogFactory::GetInstance().GetLogHandle() == f.handle.get(),
          "GetLogHandle 返回刚安装的 handle");
    Check(f.handle->GetLevel() == my_log::LogLevel::kInfo, "构造后默认阈值是 kInfo");
    Check(f.sink->records.empty(), "安装句柄本身不产生日志");
}

void CaseInfoFormatting() {
    std::cout << "[3] EXT_LOG_INFO 的 fmt 渲染" << std::endl;
    Fixture f;
    EXT_LOG_INFO("hello {}", 42);
    const std::size_t want = kOnInfo ? 1u : 0u;
    Check(f.sink->records.size() == want, "按编译期开关产生 1 条 / 0 条");
    Check(RecAt(*f.sink, 0).text == (kOnInfo ? "hello 42" : "<无记录>"), "内容为 hello 42");
    Check(RecAt(*f.sink, 0).level == (kOnInfo ? my_log::Log_Level::kInfo : my_log::Log_Level::kOff),
          "级别为 kInfo");
}

void CaseMixedArgs() {
    std::cout << "[4] 多参数混合类型" << std::endl;
    Fixture f;
    EXT_LOG_INFO("{} {} {} {}", 1, std::string("dms"), 2.5f, 'c');
    const std::size_t want = kOnInfo ? 1u : 0u;
    Check(f.sink->records.size() == want, "产生 1 条日志");
    Check(RecAt(*f.sink, 0).text == (kOnInfo ? "1 dms 2.5 c" : "<无记录>"),
          "int/string/float/char 混排结果正确（实际: " + RecAt(*f.sink, 0).text + "）");
}

void CaseAllLevels() {
    std::cout << "[5] 六个级别宏（运行时阈值先设成 kTrace 全开）" << std::endl;
    Fixture f;
    f.handle->SetLevel(my_log::LogLevel::kTrace);

    std::vector<std::string> want_text;
    std::vector<my_log::Log_Level> want_level;

    EXT_LOG_TRACE("t");
    if (kOnTrace) { want_text.push_back("t"); want_level.push_back(my_log::Log_Level::kTrace); }
    EXT_LOG_DEBUG("d");
    if (kOnDebug) { want_text.push_back("d"); want_level.push_back(my_log::Log_Level::kDebug); }
    EXT_LOG_INFO("i");
    if (kOnInfo) { want_text.push_back("i"); want_level.push_back(my_log::Log_Level::kInfo); }
    EXT_LOG_WARN("w");
    if (kOnWarn) { want_text.push_back("w"); want_level.push_back(my_log::Log_Level::kWarn); }
    EXT_LOG_ERROR("e");
    if (kOnError) { want_text.push_back("e"); want_level.push_back(my_log::Log_Level::kError); }
    EXT_LOG_CRITICAL("c");
    if (kOnCrit) { want_text.push_back("c"); want_level.push_back(my_log::Log_Level::kFatal); }

    Check(f.sink->records.size() == want_text.size(), "条数与编译期开关一致");
    bool text_ok = true;
    bool level_ok = true;
    for (std::size_t i = 0; i < want_text.size(); ++i) {
        if (RecAt(*f.sink, i).text != want_text[i]) text_ok = false;
        if (RecAt(*f.sink, i).level != want_level[i]) level_ok = false;
    }
    Check(text_ok, "内容按 t/d/i/w/e/c 顺序");
    Check(level_ok, "level 依次 kTrace/kDebug/kInfo/kWarn/kError/kFatal");
}

void CaseSourceLocation() {
    std::cout << "[6] 宏自动填充 SourceLocation" << std::endl;
    Fixture f;
    const int call_line = __LINE__ + 1;
    EXT_LOG_INFO("loc");
    if (kOnInfo) {
        Check(f.sink->records.size() == 1, "产生 1 条日志");
        const Record& r = RecAt(*f.sink, 0);
        Check(r.file == "test_my_log.cc", "file_name 被截成文件名（实际: " + r.file + "）");
        Check(r.func.find("CaseSourceLocation") != std::string::npos,
              "func_name 是调用点函数（实际: " + r.func + "）");
        Check(r.line == call_line, "line 等于宏调用所在行");
    } else {
        Check(f.sink->records.empty(), "编译期关闭时无日志");
    }
}

void CaseRuntimeFilter() {
    std::cout << "[7] 运行时阈值 SetLevel(kWarn)" << std::endl;
    Fixture f;
    f.handle->SetLevel(my_log::LogLevel::kWarn);
    EXT_LOG_INFO("filtered");
    EXT_LOG_WARN("w {}", 2);
    EXT_LOG_ERROR("e");

    std::vector<std::string> want;
    if (kOnWarn) want.push_back("w 2");
    if (kOnError) want.push_back("e");
    Check(f.sink->records.size() == want.size(), "Info 被过滤，Warn/Error 保留");
    bool ok = true;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (RecAt(*f.sink, i).text != want[i]) ok = false;
    }
    Check(ok, "保留下来的内容与顺序正确");
}

void CaseOff() {
    std::cout << "[8] SetLevel(kOff) 全关" << std::endl;
    Fixture f;
    f.handle->SetLevel(my_log::LogLevel::kOff);
    EXT_LOG_CRITICAL("x");
    Check(f.sink->records.empty(), "所有级别都不落 sink");
}

void CaseSingleEvaluation() {
    std::cout << "[9] LOGGER_CALL 的 handle 只求值一次" << std::endl;
    Fixture f;
    int calls = 0;
    auto get_handle = [&]() -> my_log::VariabledLogHandle* {
        ++calls;
        return f.handle.get();
    };
    LOGGER_CALL(get_handle(), my_log::LogLevel::kInfo, "once {}", 9);
    Check(calls == 1, "判空与 Log 用的是同一次取值");
    Check(f.sink->records.size() == 1, "产生 1 条日志");
    Check(RecAt(*f.sink, 0).text == "once 9", "内容与 fmt 渲染正确");
}

void CaseNullHandle() {
    std::cout << "[10] 未安装句柄时宏不崩" << std::endl;
    my_log::LogFactory::GetInstance().SetLogHandle(nullptr);
    EXT_LOG_INFO("no handle");
    Check(my_log::LogFactory::GetInstance().GetLogHandle() == nullptr, "空句柄被 if 判掉");
}

void CaseCompileTimeSwitch() {
    std::cout << "[11] 编译期开关与参数副作用" << std::endl;
    Fixture f;
#if LOGGER_ACTIVE_LEVEL > LOGGER_LEVEL_TRACE
    int side = 0;
    EXT_LOG_TRACE(++side);
    Check(side == 0, "被 (void)0 关掉的级别整体吞掉实参，副作用不发生");
#else
    int side = 0;
    EXT_LOG_TRACE("side {}", ++side);
    Check(side == 1, "编译期开启时参数照常求值");
    Check(f.sink->records.empty(), "但运行时阈值 kInfo 把 Trace 过滤掉（参数白算）");
#endif
}

void CaseEmptyFormat() {
    std::cout << "[12] 空格式串是合法用法" << std::endl;
    Fixture f;
    EXT_LOG_INFO("");
    if (kOnInfo) {
        Check(f.sink->records.size() == 1, "空格式串仍产生 1 条");
        Check(RecAt(*f.sink, 0).text.empty(), "内容为空字符串");
    } else {
        Check(f.sink->records.empty(), "编译期关闭时无日志");
    }
}

void CaseMultiSink() {
    std::cout << "[13] 多 sink 顺序分发" << std::endl;
    auto a = std::make_shared<RecordingSink>();
    auto b = std::make_shared<RecordingSink>();
    auto handle = std::make_shared<my_log::VariabledLogHandle>(my_log::LogSinkPtrList{a, b});
    EXT_LOGGER_INIT(handle);
    EXT_LOG_INFO("both");
    const std::size_t want = kOnInfo ? 1u : 0u;
    Check(a->records.size() == want && b->records.size() == want, "两个 sink 各收到 1 条");
    Check(RecAt(*a, 0).text == (kOnInfo ? "both" : "<无记录>"), "第一个 sink 内容正确");
    Check(RecAt(*b, 0).text == (kOnInfo ? "both" : "<无记录>"), "第二个 sink 内容正确");
    my_log::LogFactory::GetInstance().SetLogHandle(nullptr);
}

void CaseFmtIncludeOrder() {
    std::cout << "[14] 抢先 include fmt 的翻译单元能链接" << std::endl;
    my_log::LogFactory::GetInstance().SetLogHandle(nullptr);
    Check(FmtOrderProbe() == 7, "FMT_HEADER_ONLY 由编译定义全局给出，include 顺序无关");
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "===== test_my_log：对外宏 my_log/my_log.h =====" << std::endl;

    CaseHeaderConstants();
    CaseInit();
    CaseInfoFormatting();
    CaseMixedArgs();
    CaseAllLevels();
    CaseSourceLocation();
    CaseRuntimeFilter();
    CaseOff();
    CaseSingleEvaluation();
    CaseNullHandle();
    CaseCompileTimeSwitch();
    CaseEmptyFormat();
    CaseMultiSink();
    CaseFmtIncludeOrder();

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "通过 " << g_pass << " 项，失败 " << g_fail << " 项" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
