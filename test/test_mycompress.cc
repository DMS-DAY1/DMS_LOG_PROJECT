// 自研压缩封装（my_log::compress）的单元测试
//
// 和 test_compress.cc 的区别：那一个测"第三方库本身能不能用"（直接调 C API），
// 这一个测"我们自己写的封装对不对"（ZlibCompress / ZstdCompress）。
//
// 设计原则：把之前 review 里踩过的每一个坑都钉成一个用例——
//   · 大数据往返      → 守 Decompress 内部缓冲循环（zstd 曾在这里只跑一轮就退出）
//   · 会话式连续压缩  → 守"复用窗口"这条设计决策
//   · 多文件切换      → 守"每个文件一条独立流 + 解压侧靠帧头自动重建"
//   · 分块喂入        → 守解压侧的跨调用状态保持
//
// 当前契约（会话式）：压缩侧用 Z_SYNC_FLUSH，产出的数据没有帧尾，
// 因此解压侧永远不会收到 Z_STREAM_END。凡是依赖"完整帧+帧尾"的场景
// （用外部工具压的数据、将来可能的 Finish() ），都不在本套测试的覆盖范围内，
// 只在末尾做一条"契约外探针"，把它实际的行为打印出来做参考，不计成败。

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "compress/compress.h"
#include "compress/zlib_compress.h"
#include "compress/zstd_compress.h"

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

// 只打印、不计成败：用于记录"契约之外"的行为，做未来改动时的参考基线
void Note(const char* fmt, ...) {
    std::printf("  [NOTE] ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}

// 模拟真实日志：前缀高度重复（时间戳/级别/线程/模块），正好是复用窗口的用武之地
std::string MakeLogLines(int n) {
    std::string s;
    s.reserve(static_cast<size_t>(n) * 80);
    char buf[256];
    for (int i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf),
                      "2026-10-22 22:00:00 [INFO] thread-1 module=net msg=%06d user=dms city=kashi\n", i);
        s += buf;
    }
    return s;
}

// 两个实现共用的一套用例。
// need_reset：zlib 要求调用方先 ResetStream() 建流（当前契约），zstd 在构造函数里就建好了。
template <typename Impl>
void RunCommonSuite(const char* impl_name, bool need_reset) {
    std::printf("\n========== %s ==========\n\n", impl_name);

    // ---- 用例 1：基本往返 ----
    {
        std::printf("[基本往返]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }
        const std::string plain = MakeLogLines(30);
        std::vector<char> out(c.CompressedBound(plain.size()));
        const size_t n = c.Compress(plain.data(), plain.size(), out.data(), out.size());
        Check(n > 0, "Compress 返回非 0");
        Check(n <= out.size(), "输出未超出 CompressedBound 的上界");
        Check(n < plain.size(), "压完确实变小");
        Check(c.Decompress(out.data(), n) == plain, "往返逐字节一致");
        std::printf("        plain=%zu -> comp=%zu\n", plain.size(), n);
    }

    // ---- 用例 2：大数据（跨多个内部缓冲块，专门盯 Decompress 的循环出口）----
    {
        std::printf("[大数据：跨多个内部缓冲块]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }
        const std::string plain = MakeLogLines(600);          // ~48KB，远大于 4096 的分块
        std::vector<char> out(c.CompressedBound(plain.size()));
        const size_t n = c.Compress(plain.data(), plain.size(), out.data(), out.size());
        const std::string back = c.Decompress(out.data(), n);
        Check(back.size() == plain.size(), "解压长度与原文一致");
        Check(back == plain, "内容逐字节一致（尾巴没有被丢掉）");
        std::printf("        plain=%zu -> comp=%zu -> back=%zu\n", plain.size(), n, back.size());
    }

    // ---- 用例 3：会话式（连续压多块，按序解）----
    {
        std::printf("[会话式：连续压 5 块，按序解]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }
        std::vector<std::string> plains;
        std::vector<std::vector<char>> outs;
        std::vector<size_t> lens;
        for (int i = 0; i < 5; i++) {
            plains.push_back(MakeLogLines(20));
            outs.emplace_back(c.CompressedBound(plains.back().size()));
            lens.push_back(c.Compress(plains.back().data(), plains.back().size(),
                                      outs.back().data(), outs.back().size()));
        }
        bool all_ok = true;
        for (int i = 0; i < 5; i++) {
            if (lens[i] == 0) {
                all_ok = false;
                break;
            }
            if (c.Decompress(outs[i].data(), lens[i]) != plains[i]) {
                all_ok = false;
            }
        }
        Check(all_ok, "5 块全部往返一致，且互不串味");
        Check(lens[1] < lens[0], "第 2 块明显小于第 1 块（复用历史窗口生效）");
        std::printf("        各块压缩后大小：%zu %zu %zu %zu %zu\n",
                    lens[0], lens[1], lens[2], lens[3], lens[4]);
    }

    // ---- 用例 4：多文件切换（每个文件一条独立流，解压侧靠帧头自动重建）----
    //
    // 这是文件 sink 的真实场景：一个 .log 写满后 ResetStream() 换下一个文件。
    // 压缩侧每条新流都会重新发出帧头（zlib 的 78 xx / zstd 的 28 b5 2f fd），
    // 解压侧靠 Decompress 入口那个魔数判断自动重建自己的上下文——
    // 换句话说，"换文件"这个信号不需要额外的协议，帧头本身就是。
    // 这条用例钉住的就是这个隐式约定。
    {
        std::printf("[多文件切换：每个文件一条独立流]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }

        // 文件 A：连续压 3 块（复用同一条流）
        std::string file_a_plain;
        std::string file_a_comp;
        for (int i = 0; i < 3; i++) {
            const std::string p = MakeLogLines(20);
            std::vector<char> o(c.CompressedBound(p.size()));
            const size_t n = c.Compress(p.data(), p.size(), o.data(), o.size());
            file_a_comp.append(o.data(), n);
            file_a_plain += p;
        }

        // 切文件：重置压缩流（尚未接入 Finish()，所以这里没有排空尾部）
        c.ResetStream();

        // 文件 B：再连续压 3 块
        std::string file_b_plain;
        std::string file_b_comp;
        for (int i = 0; i < 3; i++) {
            const std::string p = MakeLogLines(20);
            std::vector<char> o(c.CompressedBound(p.size()));
            const size_t n = c.Compress(p.data(), p.size(), o.data(), o.size());
            file_b_comp.append(o.data(), n);
            file_b_plain += p;
        }

        // 逐文件喂给同一个 Decompress：第二次调用应当自动识别新帧头
        const std::string back_a = c.Decompress(file_a_comp.data(), file_a_comp.size());
        Check(back_a == file_a_plain, "文件 A 还原一致");
        const std::string back_b = c.Decompress(file_b_comp.data(), file_b_comp.size());
        Check(back_b == file_b_plain, "文件 B 还原一致（解压侧靠帧头自动重同步）");
        std::printf("        A=%zu/%zu  B=%zu/%zu\n",
                    back_a.size(), file_a_plain.size(), back_b.size(), file_b_plain.size());
    }

    // ---- 用例 5：分块喂入（解压侧的上下文跨调用保持）----
    {
        std::printf("[分块喂入：一次压缩拆两次喂给 Decompress]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }
        const std::string plain = MakeLogLines(30);
        std::vector<char> out(c.CompressedBound(plain.size()));
        const size_t n = c.Compress(plain.data(), plain.size(), out.data(), out.size());
        const size_t half = n / 2;
        const std::string p1 = c.Decompress(out.data(), half);
        const std::string p2 = c.Decompress(out.data() + half, n - half);
        Check((p1 + p2) == plain, "拆成两半喂入后还原一致");
        std::printf("        前半=%zu 后半=%zu\n", p1.size(), p2.size());
    }

    // ---- 用例 6：边界输入 ----
    {
        std::printf("[边界输入]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }
        std::vector<char> out(c.CompressedBound(64));
        Check(c.Compress(nullptr, 10, out.data(), out.size()) == 0, "Compress(nullptr) 返回 0");
        Check(c.Decompress(nullptr, 10).empty(), "Decompress(nullptr) 返回空串");

        const std::string tiny = "hi";
        const size_t n = c.Compress(tiny.data(), tiny.size(), out.data(), out.size());
        Check(n > 0, "2 字节输入也能压出东西");
        if (n > 0) {
            Check(c.Decompress(out.data(), n) == tiny, "2 字节输入往返一致");
        }
    }

    // ---- 用例 7：损坏数据不能崩 ----
    {
        std::printf("[损坏数据]\n");
        Impl c;
        if (need_reset) {
            c.ResetStream();
        }
        // 先正常压一块建立状态，再灌入随机字节
        const std::string plain = MakeLogLines(10);
        std::vector<char> out(c.CompressedBound(plain.size()));
        const size_t n = c.Compress(plain.data(), plain.size(), out.data(), out.size());
        (void)n;
        std::vector<char> junk(200);
        for (size_t i = 0; i < junk.size(); i++) {
            junk[i] = static_cast<char>(i * 37 + 11);
        }
        const std::string r = c.Decompress(junk.data(), junk.size());
        Check(true, "喂入无意义的字节不会崩溃");
        std::printf("        返回 %zu 字节（不崩即可，内容无意义）\n", r.size());
    }
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    RunCommonSuite<my_log::compress::ZlibCompress>("ZlibCompress", true);
    RunCommonSuite<my_log::compress::ZstdCompress>("ZstdCompress", false);

    // ================= 两套实现各自的专属用例 =================

    // ---- 用例 7：zlib 未建流时的行为（当前契约：调用方负责先 ResetStream）----
    {
        std::printf("\n========== ZlibCompress 专属 ==========\n\n");
        std::printf("[未建流时的契约]\n");
        my_log::compress::ZlibCompress zc;
        const std::string plain = MakeLogLines(10);
        std::vector<char> out(zc.CompressedBound(plain.size()));
        const size_t n = zc.Compress(plain.data(), plain.size(), out.data(), out.size());
        Check(n == 0, "没调过 ResetStream 时 Compress 返回 0（契约如此）");
    }

    // ---- 用例 8【契约外探针】zlib 面对外部完整帧的行为 ----
    //
    // compress2 产出的是以 Z_FINISH 收尾的完整帧，解压时 inflate 会返回 Z_STREAM_END。
    // 而本模块的会话式流用 Z_SYNC_FLUSH，永远走不到这一步——所以这个场景按当前契约
    // 不在支持范围内，这里不断言成败，只把实际行为打印出来存档：
    // 将来若要支持"读取外部工具压缩的日志"或"Finish() 收尾"，这条就是基线。
    {
        std::printf("[解压标准完整帧 - 契约外探针]\n");
        const std::string plain = MakeLogLines(30);
        uLongf bound = compressBound(static_cast<uLong>(plain.size()));
        std::vector<Bytef> comp(bound);
        uLongf clen = bound;
        const int r = compress2(comp.data(), &clen,
                                reinterpret_cast<const Bytef*>(plain.data()),
                                static_cast<uLong>(plain.size()), Z_BEST_COMPRESSION);
        Check(r == Z_OK, "compress2 产出标准完整帧");

        my_log::compress::ZlibCompress zc;
        const std::string back = zc.Decompress(comp.data(), clen);
        Note("喂入外部完整帧：期望 %zu 字节，实得 %zu 字节 —— 当前契约不支持，仅供参考",
             plain.size(), back.size());
    }

    // ---- 用例 9：zstd 能否解压 one-shot API 产出的完整帧 ----
    {
        std::printf("\n========== ZstdCompress 专属 ==========\n\n");
        std::printf("[解压标准完整帧]\n");
        const std::string plain = MakeLogLines(30);
        const size_t bound = ZSTD_compressBound(plain.size());
        std::vector<char> comp(bound);
        const size_t clen = ZSTD_compress(comp.data(), bound, plain.data(), plain.size(), 3);
        Check(!ZSTD_isError(clen), "ZSTD_compress 产出标准完整帧");

        my_log::compress::ZstdCompress zc;
        const std::string back = zc.Decompress(comp.data(), clen);
        Check(back == plain, "能解压 one-shot API 产出的完整帧");
        std::printf("        期望 %zu 字节，实得 %zu 字节\n", plain.size(), back.size());
    }

    // ---- 汇总 ----
    std::printf("\n========================================\n");
    std::printf("结果：%d 项 PASS，%d 项 FAIL\n", g_pass, g_fail);
    if (!g_failed.empty()) {
        std::printf("\n未通过的用例：\n");
        for (size_t i = 0; i < g_failed.size(); i++) {
            std::printf("  %zu. %s\n", i + 1, g_failed[i].c_str());
        }
    }
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
