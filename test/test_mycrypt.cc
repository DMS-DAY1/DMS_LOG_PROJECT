// 自研加密封装（my_log::crypt）的单元测试
//
// 和 test_cryptopp.cc 的区别：那一个测"Crypto++ 第三方库本身能不能用"，
// 这一个测"我们自己的封装写得对不对"（AESCrypt + ECDH 那几个自由函数）。
//
// 设计原则：把 review 里踩过的每一个坑都钉成一个用例，让它们无法悄悄回归——
//   · hex 往返            → 守 HexKeyToBinary 的编解码方向（曾写成 HexEncoder）
//   · 随机 IV             → 守"IV 每帧随机"（曾写死成常量，导致相同明文→相同密文）
//   · 多帧拼接            → 守密文帧格式（曾有长度字段则丢数据、无则静默截断）
//   · 截断/残留必须报错   → 守"宁可整体失败，也不返回一段被悄悄截断的数据"
//
// 密文帧格式（见 crypt/aes_crypt.cc 顶部）：
//     [ 16 字节 IV ][ 4 字节密文长度（大端） ][ 密文（含 PKCS#7 填充） ]
//
// 已知的、暂时用 NOTE 记录而非断言的短板：
//   · CBC 没有完整性保护，改密文中间字节解出来是乱码却不报错（要换 GCM）
//   · ECDH 协商出的共享密钥没有过 KDF 就直接当 AES key

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "crypt/aes_crypt.h"
#include "crypt/crypt.h"

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

void Check(bool ok, const std::string& what) {
    Check(ok, what.c_str());
}

// 只打印、不计成败：记录"当前就是这样"的行为，作为将来改进时的参考基线
void Note(const char* fmt, ...) {
    std::printf("  [NOTE] ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}

// 调用 fn，返回它是否抛出了异常
template <typename Fn>
bool Throws(Fn fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    } catch (...) {
        return true;
    }
    return false;
}

std::string MakeLogLine(int i) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "2026-10-24 22:00:00 [INFO] thread-1 module=net msg=%06d user=dms\n", i);
    return std::string(buf);
}

std::string MakeLogLines(int n) {
    std::string s;
    s.reserve(static_cast<size_t>(n) * 80);
    for (int i = 0; i < n; i++) {
        s += MakeLogLine(i);
    }
    return s;
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    setvbuf(stdout, nullptr, _IONBF, 0);

    using namespace my_log::crypt;
    const std::string key = AESCrypt::GenerateKey();

    // ============================================================
    std::printf("\n========== 编解码工具函数 ==========\n\n");
    // ============================================================
    {
        std::printf("[BinaryKeyToHex / HexKeyToBinary]\n");

        std::string raw;
        raw.push_back(0x01);
        raw.push_back(0x02);
        raw.push_back(0x03);
        raw.push_back(static_cast<char>(0xff));
        raw.push_back('\0');
        raw.push_back('A');

        const std::string hex = BinaryKeyToHex(raw);
        const std::string back = HexKeyToBinary(hex);
        Check(hex.size() == raw.size() * 2, "hex 长度是原始数据的两倍");
        Check(back == raw, "往返一致（含 0xff 和 \\0 这两个边界字节）");
        std::printf("        raw=%zu 字节 -> hex=\"%s\" -> back=%zu 字节\n",
                    raw.size(), hex.c_str(), back.size());

        // 大小写混写也应当能解
        Check(HexKeyToBinary("aBcD01") == HexKeyToBinary("ABCD01"), "hex 大小写不敏感");

        // 非 hex 字符：Crypto++ 的 HexDecoder 会静默丢弃，不报错
        {
            Check(HexKeyToBinary("zzzz").empty(), "非法字符被静默丢弃，产出空串");
            Check(HexKeyToBinary("0a0b").size() == 2, "合法 hex 能正常解码");
            Note("HexKeyToBinary 遇到非法字符不报错（Crypto++ 默认丢弃）—— "
                 "密钥若来自配置文件，解回来之后要自己校验长度，否则可能拿着一个空密钥往下走");
        }
    }

    // ============================================================
    std::printf("\n========== AESCrypt：随机 IV ==========\n\n");
    // ============================================================
    {
        std::printf("[相同明文必须产出不同密文]\n");
        AESCrypt c1(key);
        AESCrypt c2(key);
        const std::string plain = MakeLogLine(0);

        std::string o1, o2, o3;
        c1.Encrypt(plain.data(), plain.size(), o1);
        c1.Encrypt(plain.data(), plain.size(), o2);
        c2.Encrypt(plain.data(), plain.size(), o3);

        Check(o1 != o2, "同一实例连压两次相同明文，密文不同");
        Check(o1 != o3, "跨实例（同密钥）压相同明文，密文不同");

        // 只差最后一个字符的两条日志，密文前缀也必须不同——
        // 这正是固定 IV 时代泄露信息的那条用例
        const std::string plain_a = "2026-10-24 12:00:00 [INFO] thread-1 module=net hello worlD";
        const std::string plain_b = "2026-10-24 12:00:00 [INFO] thread-1 module=net hello worlX";
        std::string oa, ob;
        c1.Encrypt(plain_a.data(), plain_a.size(), oa);
        c1.Encrypt(plain_b.data(), plain_b.size(), ob);
        Check(oa.compare(0, 32, ob, 0, 32) != 0, "仅差末字符的两条日志，密文前缀不再相同");
    }
    {
        std::printf("[GenerateIV]\n");
        const std::string iv1 = AESCrypt::GenerateIV();
        const std::string iv2 = AESCrypt::GenerateIV();
        Check(iv1.size() == 16, "返回 16 字节原始随机量（不是 32 字符 hex）");
        Check(iv1 != iv2, "两次调用结果不同");
    }

    // ============================================================
    std::printf("\n========== AESCrypt：多帧 ==========\n\n");
    // ============================================================
    {
        std::printf("[多帧拼接后一次性解回]\n");
        AESCrypt c(key);
        std::string expected;
        std::vector<std::string> frames;
        for (int i = 0; i < 10; i++) {
            const std::string line = MakeLogLine(i);
            expected += line;
            std::string frame;
            c.Encrypt(line.data(), line.size(), frame);
            frames.push_back(frame);
        }

        std::string blob;
        for (const std::string& f : frames) {
            blob += f;
        }
        const std::string back = c.Decrypt(blob.data(), blob.size());
        Check(back == expected, "10 帧拼成一条字节流后一次性解回，内容完整");
        std::printf("        期望 %zu 字节，实得 %zu 字节\n", expected.size(), back.size());
    }
    {
        std::printf("[同一个 output 连续 Encrypt（append 语义）]\n");
        AESCrypt c(key);
        const std::string a = "first log line\n";
        const std::string b = "second log line\n";
        const std::string d = "third log line, a bit longer than the others\n";

        std::string reuse;
        c.Encrypt(a.data(), a.size(), reuse);
        const size_t after1 = reuse.size();
        c.Encrypt(b.data(), b.size(), reuse);
        Check(reuse.size() > after1, "第二次 Encrypt 是追加而不是覆盖");
        c.Encrypt(d.data(), d.size(), reuse);
        Check(c.Decrypt(reuse.data(), reuse.size()) == a + b + d, "累积三帧后一次性解回，顺序正确");
    }
    {
        std::printf("[空明文]\n");
        AESCrypt c(key);
        std::string out;
        const std::string payload = "not empty";
        c.Encrypt(payload.data(), 0, out);
        Check(!out.empty(), "空明文也会产出一帧（不是空字节流）");
        Check(c.Decrypt(out.data(), out.size()).empty(), "空明文的帧能解回空串");
    }

    // ============================================================
    std::printf("\n========== AESCrypt：数据与边界 ==========\n\n");
    // ============================================================
    {
        std::printf("[大数据]\n");
        AESCrypt c(key);
        const std::string plain = MakeLogLines(15000);  // ~1.2MB
        std::string out;
        c.Encrypt(plain.data(), plain.size(), out);
        Check(c.Decrypt(out.data(), out.size()) == plain, "1.2MB 数据往返一致");
        std::printf("        plain=%zu -> cipher=%zu\n", plain.size(), out.size());
    }
    {
        std::printf("[二进制安全：含 \\0 的全部字节值]\n");
        AESCrypt c(key);
        std::string bin;
        for (int i = 0; i < 256; i++) {
            bin.push_back(static_cast<char>(i));
        }
        for (int repeat = 0; repeat < 40; repeat++) {
            bin.append(bin.data(), 256);
        }
        std::string out;
        c.Encrypt(bin.data(), bin.size(), out);
        Check(c.Decrypt(out.data(), out.size()) == bin, "含 \\0 的二进制数据逐字节一致");
    }
    {
        std::printf("[空指针与空输入]\n");
        AESCrypt c(key);
        Check(c.Decrypt(nullptr, 100).empty(), "Decrypt(nullptr) 返回空串而不是崩溃");
        Check(c.Decrypt(key.data(), 0).empty(), "Decrypt(空字节流) 返回空串");
        const std::string payload = "x";
        Check(Throws([&] { c.Encrypt(nullptr, 10, const_cast<std::string&>(payload)); }),
              "Encrypt(nullptr) 抛异常");
    }
    {
        std::printf("[非法密钥长度]\n");
        const std::string short_key(8, 'A');
        AESCrypt c(short_key);
        const std::string payload = "some payload";
        std::string out;
        Check(Throws([&] { c.Encrypt(payload.data(), payload.size(), out); }),
              "密钥长度非法时抛异常（由 Crypto++ 的 ThrowIfInvalidKeyLength 兜住）");
    }

    // ============================================================
    std::printf("\n========== AESCrypt：损坏数据必须报错 ==========\n\n");
    // ============================================================
    {
        AESCrypt c(key);
        const std::string plain = "some log content that we will then damage on purpose";
        std::string cipher;
        c.Encrypt(plain.data(), plain.size(), cipher);
        std::printf("        原始密文 %zu 字节\n", cipher.size());

        Check(Throws([&] { c.Decrypt(cipher.data(), cipher.size() - 5); }),
              "字节流被截断时抛异常（而不是静默丢数据）");

        // 在尾部多塞 3 个无法构成帧头的字节
        std::string with_tail = cipher;
        with_tail.append(3, '\0');
        Check(Throws([&] { c.Decrypt(with_tail.data(), with_tail.size()); }),
              "尾部残留不足一帧头时抛异常");

        std::string tail_flipped = cipher;
        tail_flipped[tail_flipped.size() - 1] ^= 0x01;
        Check(Throws([&] { c.Decrypt(tail_flipped.data(), tail_flipped.size()); }),
              "末字节被改时能发现（PKCS#7 填充校验）");

        // 改密文中间：CBC 没有 MAC，大概率解出乱码却不报错 —— 这是当前契约的已知短板
        std::string mid_flipped = cipher;
        mid_flipped[20] ^= 0x01;
        std::string got;
        bool threw = false;
        try {
            got = c.Decrypt(mid_flipped.data(), mid_flipped.size());
        } catch (const std::exception&) {
            threw = true;
        }
        Check(threw || got != plain, "篡改确实生效了（要么报错，要么解出的不是原文）");
        Note("改中间字节的表现：%s —— CBC 无完整性保护，这条要等换成 GCM 才能堵住",
             threw ? "抛异常" : "未报错，解出乱码但假装成功");
    }

    // ============================================================
    std::printf("\n========== 真实链路：压缩输出 -> 加密 -> 落盘 -> 读回 ==========\n\n");
    // ============================================================
    {
        // compress 模块产出的是连续字节流，本用例把两次压缩输出各自成一帧加密，
        // 模拟 sink 分块落盘、再整块读回解密的场景
        AESCrypt c(key);
        const std::string chunk1(5000, 'A');
        const std::string chunk2(3000, 'B');
        const std::string chunk3(1234, 'C');

        std::string file_bytes;
        c.Encrypt(chunk1.data(), chunk1.size(), file_bytes);
        c.Encrypt(chunk2.data(), chunk2.size(), file_bytes);
        c.Encrypt(chunk3.data(), chunk3.size(), file_bytes);

        const std::string restored = c.Decrypt(file_bytes.data(), file_bytes.size());
        Check(restored == chunk1 + chunk2 + chunk3,
              "三次压缩输出各自成帧，读回后按序拼接完整");

        // 分两次读文件（模拟分批 read）也应当能解
        const size_t half = file_bytes.size() / 2;
        std::string part1(file_bytes.data(), half);
        std::string part2(file_bytes.data() + half, file_bytes.size() - half);
        Note("把一条字节流从中间劈开分两次解密：%s",
             Throws([&] { c.Decrypt(part2.data(), part2.size()); })
                 ? "后半段解不开（帧被切断）—— 落盘时必须保证一帧不被拆到两次 write 里"
                 : "能解");
    }

    // ============================================================
    std::printf("\n========== ECDH 密钥协商 ==========\n\n");
    // ============================================================
    {
        auto client = GenECDHKey();
        auto server = GenECDHKey();

        Check(std::get<0>(client).size() == 32, "私钥 32 字节（secp256r1）");
        Check(std::get<1>(client).size() == 65, "公钥 65 字节（未压缩点）");

        const std::string s1 = GenECDHSharedSecret(std::get<0>(client), std::get<1>(server));
        const std::string s2 = GenECDHSharedSecret(std::get<0>(server), std::get<1>(client));
        Check(s1 == s2, "双方算出的共享密钥一致");
        Check(s1.size() == 32, "共享密钥 32 字节");
        std::printf("        shared secret = %zu 字节\n", s1.size());

        // 协商结果长度凑巧合法，可以直接当密钥用；但规范做法是先过 KDF
        AESCrypt c(s1);
        const std::string payload = "payload encrypted with a negotiated key";
        std::string out;
        c.Encrypt(payload.data(), payload.size(), out);
        Check(c.Decrypt(out.data(), out.size()) == payload, "共享密钥可直接用于 AESCrypt（往返正常）");
        Note("共享密钥未过 KDF 就直接用 —— 建议接 HKDF 派生到 16/24/32 字节再当密钥");

        // 对方公钥被破坏时的几种情形
        auto other = GenECDHKey();
        {
            const std::string normal = GenECDHSharedSecret(std::get<0>(client), std::get<1>(other));

            // 尾部多一个字节：被静默忽略，协商结果和正常公钥一模一样
            const std::string extra = GenECDHSharedSecret(std::get<0>(client), std::get<1>(other) + "x");
            Check(extra == normal, "公钥尾部多余字节被忽略（结果与正常公钥一致）");
            Note("ECDH 不校验公钥长度，多余字节被静默忽略 —— 接网络数据时应在调用前自己校验 65 字节");

            // 长度不够：dh.Agree 返回 false，我们的实现会抛 runtime_error
            Check(Throws([&] {
                GenECDHSharedSecret(std::get<0>(client), std::get<1>(other).substr(0, 30));
            }), "公钥长度不足时抛异常（由 dh.Agree 返回 false 兜住）");
        }
    }

    // ============================================================
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
