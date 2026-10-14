#include "mmap/mmap.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

using my_log::Mmap;

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

// 用例④：对象销毁后重新打开，数据应还在（验证 magic 的作用）
void TestReopen() {
    Clean();
    {
        Mmap mm(kTestFile);
        mm.Push("hello", 5);
        CHECK(mm.Size() == 5, "重开前 Size 应为 5");
    }  // 这里析构，映射和句柄都应释放干净
    {
        Mmap mm2(kTestFile);
        CHECK(mm2.Size() == 5, "重开后 Size 应仍为 5");
        CHECK(std::memcmp(mm2.Data(), "hello", 5) == 0, "重开后内容应仍为 hello");
    }
    std::cout << "用例④ 重开恢复\n";
}

// 用例⑤：写入超过默认容量 512KB，触发扩容
void TestExpand() {
    Clean();
    const size_t kBlock = 4096;
    const size_t kCount = 200;  // 200 * 4KB = 800KB，远超 512KB
    std::vector<char> block(kBlock);
    {
        Mmap mm(kTestFile);
        for (size_t i = 0; i < kCount; ++i) {
            // 每块填不同字符，这样丢了哪一块都能查出来
            std::memset(block.data(), static_cast<int>('A' + (i % 26)), kBlock);
            mm.Push(block.data(), kBlock);
        }
        CHECK(mm.Size() == kBlock * kCount, "扩容后 Size 应为 819200");
        CHECK(mm.Data()[0] == 'A', "首块首字节应为 A");
        CHECK(mm.Data()[kBlock * 100] == 'A' + (100 % 26), "第 100 块首字节应对得上");
        CHECK(mm.Data()[kBlock * (kCount - 1)] == 'A' + ((kCount - 1) % 26), "末块首字节应对得上");
        CHECK(mm.GetRatio() > 0 && mm.GetRatio() <= 1.0, "GetRatio 应在 (0, 1] 之间");
    }
    {
        // 重开再验一次：确认扩容后的数据真的落到了磁盘上
        Mmap mm(kTestFile);
        CHECK(mm.Size() == kBlock * kCount, "重开后 Size 应保留");
        CHECK(mm.Data()[kBlock * (kCount - 1)] == 'A' + ((kCount - 1) % 26), "重开后末块内容应保留");
    }
    std::cout << "用例⑤ 扩容\n";
}

// 用例⑥：清空后可继续写入
void TestClear() {
    Clean();
    Mmap mm(kTestFile);
    mm.Push("hello", 5);
    CHECK(mm.Size() == 5, "清空前 Size 应为 5");
    mm.Clear();
    CHECK(mm.Size() == 0, "清空后 Size 应为 0");
    CHECK(mm.Empty(), "清空后应为空");
    mm.Push("world", 5);
    CHECK(mm.Size() == 5, "清空后应能继续写入");
    CHECK(std::memcmp(mm.Data(), "world", 5) == 0, "清空后写入内容应为 world");
    std::cout << "用例⑥ 清空\n";
}

int main() {
#ifdef _WIN32
    // 让 Windows 控制台按 UTF-8 显示输出，否则中文会变乱码
    SetConsoleOutputCP(CP_UTF8);
#endif
    TestCreate();
    TestPushOnce();
    TestPushMulti();
    TestReopen();
    TestExpand();
    TestClear();
    std::cout << (g_failed == 0 ? "全部通过\n" : "有失败用例\n");
    return g_failed == 0 ? 0 : 1;
}
