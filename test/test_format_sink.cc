#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "formatter/defaultformatter.h"
#include "log_msg/log_msg.h"
#include "sink/consolesink.h"
#include "utils/sys_util.h"

#ifdef _WIN32
#include <windows.h>

#include <fcntl.h>

#include <io.h>
#else
#include <unistd.h>
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

std::string FormatOne(const my_log::LogMsg& msg) {
    my_log::DefaultFormatter fmt;
    std::string buf;
    fmt.Format(msg, &buf);
    return buf;
}

// 构造一条带完整 SourceLocation 的消息；路径带目录，用来验证截断
my_log::LogMsg MakeMsg(my_log::Log_Level level, const std::string& text) {
    return my_log::LogMsg(my_log::SourceLocation("src/log/main.cc", "main", 42), level, text);
}

// ============ DefaultFormatter ============

// 用例 1：整体格式 [time] [L] [file:line] [pid:tid] message
void CaseFormatStructure() {
    std::cout << "[1] DefaultFormatter 整体格式" << std::endl;
    const std::string buf = FormatOne(MakeMsg(my_log::Log_Level::Info, "hello"));

    // 时间串必须是 [2026-10-17 18:00:00] 这种定长 19 字符，']' 正好落在下标 20
    Check(buf.size() > 20 && buf.front() == '[', "以 '[' 开头");
    Check(buf.find(']') == 20, "时间串定长 19 字符（']' 在下标 20）");
    Check(buf.compare(1, 2, "20") == 0, "时间串以年份 20xx 开头");
    Check(buf.find("-") != std::string::npos && buf.find(":") != std::string::npos,
          "时间串含日期与时间分隔符");

    Check(buf.find("] [I] [") != std::string::npos, "级别段是 ] [I] [");
    Check(buf.find("[main.cc:42]") != std::string::npos, "文件名被截断成 main.cc，行号 42");

    const std::string pid_tid = "[" + std::to_string(my_log::GetProcessId()) + ":" +
                                std::to_string(my_log::GetThreadId()) + "]";
    Check(buf.find(pid_tid) != std::string::npos, "pid:tid 段与系统函数一致");

    Check(buf.size() >= 5 && buf.compare(buf.size() - 5, 5, "hello") == 0, "message 原文原样收尾");
    Check(buf.find("hello") == buf.size() - 5, "message 前面是 \"] \" 一个空格");
}

// 用例 2：七个级别字符 TDIWEFO 一一映射
void CaseLevelChars() {
    std::cout << "[2] 级别字符映射 TDIWEFO" << std::endl;
    const std::string kExpect = "TDIWEFO";
    const my_log::Log_Level levels[] = {
        my_log::Log_Level::Trance, my_log::Log_Level::Debug, my_log::Log_Level::Info,
        my_log::Log_Level::Warn,   my_log::Log_Level::Error, my_log::Log_Level::Fatal,
        my_log::Log_Level::Off,
    };
    bool all_ok = true;
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        const std::string buf = FormatOne(MakeMsg(levels[i], "x"));
        const std::string want = std::string("] [") + kExpect[i] + "] [";
        if (buf.find(want) == std::string::npos) {
            all_ok = false;
            std::cout << "      level=" << static_cast<int>(levels[i]) << " 实际输出: " << buf
                      << std::endl;
        }
    }
    Check(all_ok, "Trance/Debug/Info/Warn/Error/Fatal/Off -> T/D/I/W/E/F/O");
}

// 用例 3：SourceLocation 的路径截断（正斜杠和反斜杠都要切）
void CaseLocationTrim() {
    std::cout << "[3] SourceLocation 路径截断" << std::endl;
    const std::string b1 = FormatOne(my_log::LogMsg(
        my_log::SourceLocation("a/b/c/file.cc", "f", 7), my_log::Log_Level::Warn, "m"));
    Check(b1.find("[file.cc:7]") != std::string::npos, "正斜杠路径只留文件名");

    const std::string b2 = FormatOne(my_log::LogMsg(
        my_log::SourceLocation("C:\\dir\\sub\\file.cc", "f", 8), my_log::Log_Level::Warn, "m"));
    Check(b2.find("[file.cc:8]") != std::string::npos, "反斜杠路径只留文件名");

    const std::string b3 = FormatOne(my_log::LogMsg(
        my_log::SourceLocation("bare.cc", "f", 9), my_log::Log_Level::Warn, "m"));
    Check(b3.find("[bare.cc:9]") != std::string::npos, "无目录路径原样保留");
}

// 用例 4：空 message 不崩，且仍以 "] " 收尾
void CaseEmptyMessage() {
    std::cout << "[4] 空 message 边界" << std::endl;
    const std::string buf = FormatOne(MakeMsg(my_log::Log_Level::Info, ""));
    Check(buf.find("] ") != std::string::npos, "仍有时钟/级别/位置段");
    Check(buf.size() >= 2 && buf.compare(buf.size() - 2, 2, "] ") == 0, "以 \"] \" 结尾");
}

// 用例 5：dest 不清空——连续 Format 同一个 buf 会累加（记录当前行为）
void CaseDestAppendSemantics() {
    std::cout << "[5] dest 累加语义（Format 前不清空 dest）" << std::endl;
    my_log::DefaultFormatter fmt;
    std::string buf;
    const my_log::LogMsg msg = MakeMsg(my_log::Log_Level::Info, "hello");
    fmt.Format(msg, &buf);
    const size_t first = buf.size();
    fmt.Format(msg, &buf);
    Check(buf.size() == first * 2, "第二次 Format 后长度恰好翻倍（内容追加而非覆盖）");
}

// ============ ConsoleSink ============

// 把 stdout 重定向到文件，跑完 log 后恢复，返回捕获到的内容。
// 两个平台坑：
// 1. 不能用 freopen("/dev/stdout", ...) 恢复——/dev/stdout 是动态符号链接，
//    指向“当前的 fd 1”，而此时 fd 1 已经指向捕获文件，那句恢复会再次 O_TRUNC
//    打开同一个文件，把内容清空。正确做法是先用 dup 存一份原 fd，再 dup2 恢复。
// 2. Windows 文本模式下 C 库会把 '\n' 写成 "\r\n"，所以捕获期间要把 stdout
//    切成 binary 模式（_setmode + _O_BINARY），否则断言里对 '\n' 的精确比较全挂。
std::string CaptureConsoleLog(my_log::ConsoleSink& sink, const my_log::LogMsg& msg) {
    const std::string path = "consolesink_capture.txt";
    std::fflush(stdout);
#ifdef _WIN32
    const int saved_fd = _dup(_fileno(stdout));
#else
    const int saved_fd = dup(fileno(stdout));
#endif
    if (std::freopen(path.c_str(), "w", stdout) == nullptr) {
        return "<freopen failed>";
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    sink.Log(msg);
    std::fflush(stdout);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_TEXT);
    _dup2(saved_fd, _fileno(stdout));
    _close(saved_fd);
#else
    dup2(saved_fd, fileno(stdout));
    close(saved_fd);
#endif
    std::cout.flush();

    std::ifstream ifs(path, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::remove(path.c_str());
    return oss.str();
}

// 用例 6：默认构造 + Log，走默认格式化器，stdout 落一行
void CaseConsoleSinkDefault() {
    std::cout << "[6] ConsoleSink 默认 Log 输出" << std::endl;
    my_log::ConsoleSink sink;
    const std::string out = CaptureConsoleLog(sink, MakeMsg(my_log::Log_Level::Info, "hello"));

    Check(out.size() >= 40, "捕获到非空输出");
    Check(out.front() == '[', "以时间段的 '[' 开头");
    Check(out.back() == '\n', "以换行结尾");
    Check(out.find("] [I] [") != std::string::npos, "默认 formatter 的级别段 ] [I] [");
    Check(out.find("[main.cc:42]") != std::string::npos, "默认 formatter 的位置段");
    Check(out.find("hello\n") != std::string::npos, "message 与换行都在");
    Check(out.find('\n') == out.size() - 1, "只有一行（没有多余换行）");
}

// 用例 7：SetFormatter 换成自定义 formatter，输出完全由它决定
class UpperFormatter : public my_log::Formatter {
public:
    void Format(const my_log::LogMsg& msg, my_log::MemoryBuf* dest) override {
        dest->append("MOCK:");
        for (char c : msg.message) {
            dest->push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
};

void CaseConsoleSinkSetFormatter() {
    std::cout << "[7] ConsoleSink::SetFormatter 自定义格式" << std::endl;
    my_log::ConsoleSink sink;
    sink.SetFormatter(std::make_unique<UpperFormatter>());
    const std::string out = CaptureConsoleLog(sink, MakeMsg(my_log::Log_Level::Info, "hello"));
    Check(out == "MOCK:HELLO\n", "输出精确等于自定义格式（实际: " + out + "）");
}

// 用例 8：多态使用——基类指针删除、Flush 默认实现可调
void CaseConsoleSinkPolymorphic() {
    std::cout << "[8] ConsoleSink 多态与 Flush" << std::endl;
    std::unique_ptr<my_log::LogSink> sink = std::make_unique<my_log::ConsoleSink>();
    sink->Flush();  // 基类默认实现，应什么都不做且不崩
    Check(true, "基类指针 + Flush() 不崩");
    sink.reset();   // 虚析构
    Check(true, "unique_ptr<LogSink> 释放 ConsoleSink 不崩（虚析构生效）");
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "===== test_format_sink：DefaultFormatter + ConsoleSink =====" << std::endl;

    CaseFormatStructure();
    CaseLevelChars();
    CaseLocationTrim();
    CaseEmptyMessage();
    CaseDestAppendSemantics();
    CaseConsoleSinkDefault();
    CaseConsoleSinkSetFormatter();
    CaseConsoleSinkPolymorphic();

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "通过 " << g_pass << " 项，失败 " << g_fail << " 项" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
