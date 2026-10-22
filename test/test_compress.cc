// zlib + zstd 集成测试
// 两个库都是纯 C，通过 CMake 的 add_subdirectory 编成静态库（zlibstatic / libzstd_static），
// 本测试验证：压缩-解压往返逐字节一致、压缩率符合预期、空输入边界、版本号可读。

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "zlib.h"
#include "zstd.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what);
    }
}

// 造一段“又好压缩又能验证正确性”的数据：
// 400 行重复英文（保证压缩率明显）+ 1KB 二进制 pattern（保证不全是文本模式）
std::vector<uint8_t> MakeSampleData() {
    std::vector<uint8_t> data;
    const char* line = "The quick brown fox jumps over the lazy dog. ";
    const size_t line_len = std::strlen(line);
    for (int i = 0; i < 400; ++i) {
        data.insert(data.end(), line, line + line_len);
    }
    for (int i = 0; i < 1024; ++i) {
        data.push_back(static_cast<uint8_t>(i * 7));
    }
    return data;
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    const std::vector<uint8_t> raw = MakeSampleData();
    std::printf("样本数据大小：%zu 字节\n\n", raw.size());

    // ---------- 用例 1：zlib compress2 / uncompress 往返 ----------
    {
        std::printf("[1] zlib 压缩/解压往返\n");

        uLongf bound = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> compressed(bound);
        uLongf comp_len = bound;
        const int cret = compress2(compressed.data(), &comp_len,
                                   raw.data(), static_cast<uLong>(raw.size()),
                                   Z_BEST_COMPRESSION);
        Check(cret == Z_OK, "compress2 返回 Z_OK");
        compressed.resize(comp_len);
        std::printf("  压缩后：%zu 字节（原 %zu，比率 %.1f%%）\n",
                    compressed.size(), raw.size(),
                    100.0 * compressed.size() / raw.size());
        Check(compressed.size() < raw.size(), "压缩后确实变小");

        std::vector<uint8_t> restored(raw.size());
        uLongf rest_len = static_cast<uLong>(raw.size());
        const int uret = uncompress(restored.data(), &rest_len,
                                    compressed.data(),
                                    static_cast<uLong>(compressed.size()));
        Check(uret == Z_OK, "uncompress 返回 Z_OK");
        Check(rest_len == raw.size(), "解压后长度与原文一致");
        Check(std::memcmp(restored.data(), raw.data(), raw.size()) == 0,
              "解压内容与原文逐字节一致");
    }

    // ---------- 用例 2：zlib 空输入边界 ----------
    {
        std::printf("[2] zlib 空输入边界\n");

        uLongf bound = compressBound(0);
        std::vector<uint8_t> compressed(bound);
        uLongf comp_len = bound;
        const int cret = compress2(compressed.data(), &comp_len,
                                   raw.data(), 0, Z_DEFAULT_COMPRESSION);
        Check(cret == Z_OK, "空输入 compress2 也返回 Z_OK");

        std::vector<uint8_t> restored(1);
        uLongf rest_len = 1;
        const int uret = uncompress(restored.data(), &rest_len,
                                    compressed.data(), comp_len);
        Check(uret == Z_OK, "空输入 uncompress 返回 Z_OK");
        Check(rest_len == 0, "空输入解压出来长度 0");
    }

    // ---------- 用例 3：zstd 压缩/解压往返 ----------
    {
        std::printf("[3] zstd 压缩/解压往返\n");

        const size_t bound = ZSTD_compressBound(raw.size());
        std::vector<uint8_t> compressed(bound);
        const size_t cret = ZSTD_compress(compressed.data(), bound,
                                          raw.data(), raw.size(), 3);
        Check(!ZSTD_isError(cret), "ZSTD_compress 无错误");
        compressed.resize(cret);
        std::printf("  压缩后：%zu 字节（原 %zu，比率 %.1f%%）\n",
                    compressed.size(), raw.size(),
                    100.0 * compressed.size() / raw.size());
        Check(cret < raw.size(), "压缩后确实变小");
        Check(bound >= cret, "compressBound 估的上界 >= 实际输出");

        std::vector<uint8_t> restored(raw.size());
        const size_t uret = ZSTD_decompress(restored.data(), restored.size(),
                                            compressed.data(), compressed.size());
        Check(!ZSTD_isError(uret), "ZSTD_decompress 无错误");
        Check(uret == raw.size(), "解压后长度与原文一致");
        Check(std::memcmp(restored.data(), raw.data(), raw.size()) == 0,
              "解压内容与原文逐字节一致");
    }

    // ---------- 用例 4：zstd 空输入边界 ----------
    {
        std::printf("[4] zstd 空输入边界\n");

        const size_t bound = ZSTD_compressBound(0);
        std::vector<uint8_t> compressed(bound);
        const size_t cret = ZSTD_compress(compressed.data(), bound,
                                          raw.data(), 0, 3);
        Check(!ZSTD_isError(cret), "空输入 ZSTD_compress 无错误");

        std::vector<uint8_t> restored(1);
        const size_t uret = ZSTD_decompress(restored.data(), 1,
                                            compressed.data(), cret);
        Check(!ZSTD_isError(uret), "空输入 ZSTD_decompress 无错误");
        Check(uret == 0, "空输入解压出来长度 0");
    }

    // ---------- 用例 5：版本号冒烟 ----------
    {
        std::printf("[5] 版本号\n");
        std::printf("  zlib %s / zstd %s\n", ZLIB_VERSION, ZSTD_VERSION_STRING);
        Check(ZLIB_VERSION[0] != '\0', "ZLIB_VERSION 非空");
        Check(ZSTD_VERSION_STRING[0] != '\0', "ZSTD_VERSION_STRING 非空");
    }

    std::printf("\n结果：%d 项 PASS，%d 项 FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
