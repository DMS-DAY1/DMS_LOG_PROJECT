#include "mmap/mmap.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static const char* kTestFile = "test.mmap";

// 断言失败就打印并置为失败状态
static int g_failed = 0;
static void CHECK(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "  [失败] " << what << "\n";
        ++g_failed;
    }
}

// 每个用例前删掉旧文件，保证干净环境
static void Clean() { std::error_code ec; fs::remove(kTestFile, ec); }

// 用例①：创建
void TestCreate() {
    Clean();
    Mmap mm(kTestFile);
    CHECK(mm.Empty(), "新对象应为空");
    CHECK(mm.Size() == 0, "新对象 Size 应为 0");
    CHECK(fs::exists(kTestFile), "文件应被创建");
    std::cout << "用例① 创建\n";
}

// 用例②：单次写入并读回
void TestPushOnce() {
    Clean();
    {
        Mmap mm(kTestFile);
        mm.Push("hello", 5);
        CHECK(mm.Size() == 5, "Size 应为 5");
        CHECK(std::memcmp(mm.Data(), "hello", 5) == 0, "读回内容应为 hello");
    }
    std::cout << "用例② 单次写入\n";
}

// 用例③：多次写入累加
void TestPushMulti() {
    Clean();
    Mmap mm(kTestFile);
    mm.Push("ab", 2);
    mm.Push("cd", 2);
    mm.Push("ef", 2);
    CHECK(mm.Size() == 6, "Size 应为 6");
    CHECK(std::memcmp(mm.Data(), "abcdef", 6) == 0, "读回内容应为 abcdef");
    std::cout << "用例③ 多次写入\n";
}

int main() {
#ifdef _WIN32
    // 让 Windows 控制台按 UTF-8 显示输出，否则中文会变乱码
    SetConsoleOutputCP(CP_UTF8);
#endif
    TestCreate();
    TestPushOnce();
    TestPushMulti();
    std::cout << (g_failed == 0 ? "全部通过\n" : "有失败用例\n");
    return g_failed == 0 ? 0 : 1;
}
