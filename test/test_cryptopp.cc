// Crypto++ 集成验证：确认库能编过、能链接、能跑通
//
// 和 test_compress.cc 一个性质——测的是"第三方库在本项目里能不能用"，
// 不是测我们自己写的封装（等你写好 encrypt 模块，再另起一个 test_myencrypt）。
//
// 这里挑的都是日志加密场景真正会用到的东西：
//   · AES-GCM   —— 带完整性校验的认证加密（推荐，密文被改能察觉）
//   · AES-CBC   —— 最基础的分组加密，用来对照
//   · SHA-256   —— 摘要 / 校验
//   · Base64/Hex—— 密文要落地成文本时的编码
//
// 关于 include 写法：#include "cryptopp/aes.h"
//   路径从项目根算起，cryptopp 目标通过 PUBLIC include 目录自动传播，
//   不需要在自己工程的 CMakeLists 里手写路径，只要 target_link_libraries 即可。

#include <cstdio>
#include <string>

#include "cryptopp/aes.h"
#include "cryptopp/base64.h"
#include "cryptopp/cpu.h"
#include "cryptopp/filters.h"
#include "cryptopp/gcm.h"
#include "cryptopp/hex.h"
#include "cryptopp/modes.h"
#include "cryptopp/osrng.h"
#include "cryptopp/sha.h"

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

using CryptoPP::AES;
using CryptoPP::byte;

std::string MakeLogLines(int n) {
    std::string s;
    char buf[256];
    for (int i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf),
                      "2026-10-23 15:00:00 [INFO] thread-1 module=net msg=%06d user=dms\n", i);
        s += buf;
    }
    return s;
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    CryptoPP::AutoSeededRandomPool rng;

    // 硬件加速探测：这几项依赖 cpu.cpp 里那两个汇编函数（CPUID64 / XGETBV64）。
    // 能正确报出来，说明 MASM 那条路走通了；全是 NO 则说明退化成了纯软件实现。
    std::printf("[CPU 特性探测]\n");
    std::printf("        AES-NI=%s  CLMUL=%s  SSE2=%s  SHA=%s\n",
                CryptoPP::HasAESNI() ? "YES" : "NO",
                CryptoPP::HasCLMUL() ? "YES" : "NO",
                CryptoPP::HasSSE2()  ? "YES" : "NO",
                CryptoPP::HasSHA()   ? "YES" : "NO");

    // ---- 1. AES-GCM：认证加密，密文被篡改时能察觉 ----
    {
        std::printf("[AES-GCM 认证加密]\n");
        const std::string plain = MakeLogLines(20);

        byte key[AES::DEFAULT_KEYLENGTH];   // 16 字节
        byte iv[12];                        // GCM 推荐 12 字节
        rng.GenerateBlock(key, sizeof(key));
        rng.GenerateBlock(iv, sizeof(iv));

        std::string cipher;
        {
            CryptoPP::GCM<AES>::Encryption enc;
            enc.SetKeyWithIV(key, sizeof(key), iv, sizeof(iv));
            CryptoPP::StringSource(plain, true,
                new CryptoPP::AuthenticatedEncryptionFilter(enc,
                    new CryptoPP::StringSink(cipher)));
        }
        Check(cipher.size() == plain.size() + 16, "密文 = 明文 + 16 字节认证标签");

        std::string back;
        {
            CryptoPP::GCM<AES>::Decryption dec;
            dec.SetKeyWithIV(key, sizeof(key), iv, sizeof(iv));
            CryptoPP::StringSource(cipher, true,
                new CryptoPP::AuthenticatedDecryptionFilter(dec,
                    new CryptoPP::StringSink(back)));
        }
        Check(back == plain, "往返逐字节一致");

        // 篡改密文：应当抛异常，而不是解出一堆乱码还假装成功
        std::string tampered = cipher;
        tampered[0] ^= 0xFF;
        bool caught = false;
        try {
            std::string junk;
            CryptoPP::GCM<AES>::Decryption dec;
            dec.SetKeyWithIV(key, sizeof(key), iv, sizeof(iv));
            CryptoPP::StringSource(tampered, true,
                new CryptoPP::AuthenticatedDecryptionFilter(dec,
                    new CryptoPP::StringSink(junk)));
        } catch (const CryptoPP::Exception&) {
            caught = true;
        }
        Check(caught, "密文被篡改时抛异常（完整性校验生效）");
        std::printf("        plain=%zu -> cipher=%zu\n", plain.size(), cipher.size());
    }

    // ---- 2. AES-CBC：最基础的分组加密 ----
    {
        std::printf("[AES-CBC 分组加密]\n");
        const std::string plain = MakeLogLines(20);

        byte key[AES::DEFAULT_KEYLENGTH];
        byte iv[AES::BLOCKSIZE];
        rng.GenerateBlock(key, sizeof(key));
        rng.GenerateBlock(iv, sizeof(iv));

        std::string cipher;
        {
            CryptoPP::CBC_Mode<AES>::Encryption enc;
            enc.SetKeyWithIV(key, sizeof(key), iv);
            CryptoPP::StringSource(plain, true,
                new CryptoPP::StreamTransformationFilter(enc,
                    new CryptoPP::StringSink(cipher)));
        }

        std::string back;
        {
            CryptoPP::CBC_Mode<AES>::Decryption dec;
            dec.SetKeyWithIV(key, sizeof(key), iv);
            CryptoPP::StringSource(cipher, true,
                new CryptoPP::StreamTransformationFilter(dec,
                    new CryptoPP::StringSink(back)));
        }
        Check(back == plain, "往返逐字节一致");
        Check(cipher.size() >= plain.size(), "CBC 填充后密文不小于明文");
        std::printf("        plain=%zu -> cipher=%zu\n", plain.size(), cipher.size());
    }

    // ---- 3. SHA-256 ----
    {
        std::printf("[SHA-256 摘要]\n");
        const std::string plain = MakeLogLines(10);
        std::string digest;
        CryptoPP::SHA256 hash;
        CryptoPP::StringSource(plain, true,
            new CryptoPP::HashFilter(hash,
                new CryptoPP::HexEncoder(
                    new CryptoPP::StringSink(digest))));
        Check(digest.size() == 64, "摘要为 64 个 hex 字符（256 bit）");

        // 相同输入必须得到相同摘要；改一个字节就必须完全不同
        std::string digest2;
        std::string changed = plain;
        changed[5] = changed[5] == 'A' ? 'B' : 'A';
        CryptoPP::StringSource(changed, true,
            new CryptoPP::HashFilter(hash,
                new CryptoPP::HexEncoder(
                    new CryptoPP::StringSink(digest2))));
        Check(digest != digest2, "改动一个字节后摘要完全不同");
        std::printf("        %s\n", digest.c_str());
    }

    // ---- 4. Base64 / Hex 编解码（密文落地成文本时用）----
    {
        std::printf("[Base64 / Hex 编解码]\n");
        const std::string raw = "hello, cryptopp! \xE4\xB8\xAD\xE6\x96\x87";

        std::string b64;
        CryptoPP::StringSource(raw, true,
            new CryptoPP::Base64Encoder(new CryptoPP::StringSink(b64)));
        std::string from_b64;
        CryptoPP::StringSource(b64, true,
            new CryptoPP::Base64Decoder(new CryptoPP::StringSink(from_b64)));
        Check(from_b64 == raw, "Base64 往返一致");

        std::string hex;
        CryptoPP::StringSource(raw, true,
            new CryptoPP::HexEncoder(new CryptoPP::StringSink(hex)));
        std::string from_hex;
        CryptoPP::StringSource(hex, true,
            new CryptoPP::HexDecoder(new CryptoPP::StringSink(from_hex)));
        Check(from_hex == raw, "Hex 往返一致");
        std::printf("        base64=%s\n", b64.c_str());
    }

    std::printf("\n========================================\n");
    std::printf("结果：%d 项 PASS，%d 项 FAIL\n", g_pass, g_fail);
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
