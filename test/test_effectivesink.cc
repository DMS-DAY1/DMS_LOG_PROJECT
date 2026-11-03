// EffectiveSink 端到端测试
//
// 被测对象：sink/effectivesink.cc 里 DMS 写的 EffectiveSink
// 验证链路：构造 -> Log(格式化) -> zstd 压缩 -> AES-CBC 加密 -> 双缓冲 mmap 落盘
//          -> 像“服务器”一样读回 .log -> 用私钥 ECDH 协商出同一把 K
//          -> AES 解密 -> zstd 解压 -> protobuf 反序列化 -> 逐字段核对。
//
// 设计要点：
//   * 测试自己生成一对“服务器”密钥，把公钥 hex 喂给 Conf.pub_key，模拟真实部署。
//   * 落盘文件头 ChunkHeader.pub_key 里存的是客户端临时公钥（前 65 字节为
//     secp256r1 未压缩公钥，后面是未清零的栈空间），解密时只取前 65 字节。
//   * 多条日志是“流式连续压缩”，解密后整体解压即可还原；单条日志可做完整内容回放。

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

#include "sink/effectivesink.h"
#include "crypt/crypt.h"
#include "crypt/aes_crypt.h"
#include "compress/zstd_compress.h"
#include "log_msg/log_msg.h"
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

// 在系统临时目录下造一个带时间戳的唯一子目录当日志目录
std::filesystem::path MakeTestDir(const std::string& tag) {
    auto dir = std::filesystem::temp_directory_path() /
               ("effsink_" + tag + "_" + std::to_string(std::time(nullptr)));
    std::filesystem::create_directories(dir);
    return dir;
}

std::string ReadWholeFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// 从 ChunkHeader.pub_key 取客户端公钥字节数：
// secp256r1 未压缩公钥首字节 0x04、长度 65；压缩公钥首字节 0x02/0x03、长度 33。
size_t ClientPubLen(const char pub_key[128]) {
    unsigned char b = static_cast<unsigned char>(pub_key[0]);
    if (b == 0x04) return 65;
    if (b == 0x02 || b == 0x03) return 33;
    return 0;
}

// 把 .log 目录里第一个 .log 文件找出来
std::filesystem::path FindFirstLog(const std::filesystem::path& dir) {
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() == ".log") return e.path();
    }
    return {};
}

// 按 EffectiveSink 的真实落盘格式解析一个 chunk 的数据区：
//   [ItemHeader 16B][AES 帧] [ItemHeader 16B][AES 帧] ...
// 每条日志 = 一个 ItemHeader(记录该条加密帧字节数) + 一个独立 AES 帧。
// 返回逐条解密后拼接的明文（压缩是流式的，拼接后整体解压即可）。
std::string DecryptChunkBody(const std::string& chunk_body, const std::string& K,
                             int& item_count, bool& format_ok) {
    item_count = 0;
    format_ok = true;
    std::string plain;
    my_log::crypt::AESCrypt aes(K);

    size_t pos = 0;
    while (pos + sizeof(my_log::detail::ItemHeader) <= chunk_body.size()) {
        my_log::detail::ItemHeader ih;
        std::memcpy(&ih, chunk_body.data() + pos, sizeof(ih));
        pos += sizeof(ih);

        if (ih.magic != my_log::detail::ItemHeader::kmagic) {
            format_ok = false;
            break;
        }
        if (ih.size > chunk_body.size() - pos) {
            format_ok = false;
            break;
        }

        plain += aes.Decrypt(chunk_body.data() + pos, static_cast<size_t>(ih.size));
        pos += static_cast<size_t>(ih.size);
        ++item_count;
    }
    if (pos != chunk_body.size()) format_ok = false;
    return plain;
}

} // namespace

int main() {
#ifdef _WIN32
    // 让控制台按 UTF-8 输出，避免中文用例名/日志乱码
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::printf("==== EffectiveSink 端到端测试 ====\n");

    // 跨用例保存“服务器私钥”，用例2/3 要用来协商
    std::string srv_pri;
    std::filesystem::path log_file;

    // ---- 用例1：构造 + 写多条日志 + Flush + 落盘 ----
    {
        std::printf("[用例1] 构造 + 写多条日志 + Flush\n");
        auto srv = my_log::crypt::GenECDHKey();
        srv_pri = std::get<0>(srv);
        std::string srv_pub_hex = my_log::crypt::BinaryKeyToHex(std::get<1>(srv));

        auto dir = MakeTestDir("multi");
        my_log::EffectiveSink::Conf conf;
        conf.dif = dir;
        conf.prefix = "test";
        conf.pub_key = srv_pub_hex;
        conf.interval = std::chrono::minutes{1};
        conf.single_size = my_log::megabytes{4};
        conf.total_size = my_log::megabytes{100};

        // 对照证据：构造之前，Conf 里的值确实是有内容的。
        // 若构造后 sink 打印出来的 dir/prefix/pub_key 是空的，说明 sink 内部
        // 访问了「已被 move 走的参数 conf」，而不是成员 conf_。
        std::printf("  [对照] 构造前 conf: dif='%s' (len=%zu), prefix='%s' (len=%zu), pub_key len=%zu\n",
                    conf.dif.string().c_str(), conf.dif.string().size(),
                    conf.prefix.c_str(), conf.prefix.size(),
                    conf.pub_key.size());

        my_log::EffectiveSink sink(conf);  // 不应抛异常

        std::pair<const char*, const char*> cases[] = {
            {"a.cc", "first message"},
            {"b.cc", "second message"},
            {"中文路径.cc", "中文日志：连接数据库失败"},
        };
        for (int i = 0; i < 3; ++i) {
            my_log::LogMsg m(
                my_log::SourceLocation(cases[i].first, "func", i + 1),
                my_log::Log_Level::Info, cases[i].second);
            sink.Log(m);
        }
        sink.Flush();

        log_file = FindFirstLog(dir);
        Check(!log_file.empty(), "落盘生成了 .log 文件");
    }

    // ---- 用例2：文件头 ChunkHeader 结构（magic）----
    if (!log_file.empty()) {
        std::printf("[用例2] 文件头 ChunkHeader 结构\n");
        std::string body = ReadWholeFile(log_file);
        Check(body.size() > sizeof(my_log::detail::ChunkHeader), "文件大小 > ChunkHeader");
        my_log::detail::ChunkHeader hdr;
        std::memcpy(&hdr, body.data(), sizeof(hdr));
        Check(hdr.magic == my_log::detail::ChunkHeader::kmagic,
              "ChunkHeader.magic 正确 (0xdeadbeefdada1100)");

        size_t pub_len = ClientPubLen(hdr.pub_key);
        Check(pub_len > 0, "pub_key 首字节是合法 EC 点 (0x04 / 0x02 / 0x03)");

        // ---- 用例3：服务器私钥协商 -> AES 解密 -> zstd 解压（验证加密/压缩链路）----
        if (pub_len > 0) {
            std::printf("[用例3] 服务器私钥 ECDH 协商 -> AES 解密 -> zstd 解压\n");
            // 只取文件头里真实的客户端公钥字节（避开未清零的栈空间）
            std::string client_pub(hdr.pub_key, pub_len);
            std::string K = my_log::crypt::GenECDHSharedSecret(srv_pri, client_pub);
            Check(!K.empty(), "ECDH 协商出共享密钥 K（与客户端算出的是同一把）");

            std::string file_body = body.substr(sizeof(hdr));
            std::printf("  [诊断] K.size()=%zu, 文件头=%zu 字节, 数据区=%zu 字节, chunk.size=%llu\n",
                        K.size(), sizeof(hdr), file_body.size(),
                        static_cast<unsigned long long>(hdr.size));
            try {
                int item_count = 0;
                bool fmt_ok = false;
                std::string plain = DecryptChunkBody(file_body, K, item_count, fmt_ok);
                Check(fmt_ok, "数据区按 [ItemHeader][AES 帧] 格式完整解析");
                Check(item_count == 3, "条目数与写入条数一致（3 条）");
                std::printf("  [诊断] 解密后明文=%zu 字节, 条目数=%d\n", plain.size(), item_count);
                Check(!plain.empty(), "AES-CBC 解密成功（加密链路正确）");

                my_log::compress::ZstdCompress zc;
                std::string decompressed = zc.Decompress(plain.data(), plain.size());
                std::printf("  [诊断] 解压后=%zu 字节\n", decompressed.size());
                Check(!decompressed.empty(),
                      "zstd 解压成功（多条流式压缩整体可解压）");
                Check(decompressed.find("first message") != std::string::npos,
                      "内容回放：包含 first message");
                Check(decompressed.find("second message") != std::string::npos,
                      "内容回放：包含 second message");
                Check(decompressed.find("中文日志：连接数据库失败") != std::string::npos,
                      "内容回放：中文日志正确");
            } catch (const std::exception& e) {
                std::printf("  [异常] %s\n", e.what());
                Check(false, "AES 解密 / zstd 解压未抛异常");
            }
        }
    }

    // ---- 用例4：单条日志完整内容回放（端到端往返）----
    {
        std::printf("[用例4] 单条日志完整内容回放\n");
        auto srv = my_log::crypt::GenECDHKey();
        std::string srv_pri2 = std::get<0>(srv);
        std::string srv_pub_hex2 = my_log::crypt::BinaryKeyToHex(std::get<1>(srv));

        auto dir = MakeTestDir("single");
        my_log::EffectiveSink::Conf conf;
        conf.dif = dir;
        conf.prefix = "single";
        conf.pub_key = srv_pub_hex2;
        conf.interval = std::chrono::minutes{1};
        conf.single_size = my_log::megabytes{4};
        conf.total_size = my_log::megabytes{100};

        my_log::EffectiveSink sink(conf);
        my_log::LogMsg m(my_log::SourceLocation("b.cc", "handler", 5),
                         my_log::Log_Level::Error, "hello sink test");
        sink.Log(m);
        sink.Flush();

        std::filesystem::path lf = FindFirstLog(dir);
        Check(!lf.empty(), "单条：落盘生成 .log 文件");
        if (!lf.empty()) {
            std::string body = ReadWholeFile(lf);
            my_log::detail::ChunkHeader hdr;
            std::memcpy(&hdr, body.data(), sizeof(hdr));
            size_t pub_len = ClientPubLen(hdr.pub_key);
            if (pub_len > 0) {
                std::string client_pub(hdr.pub_key, pub_len);
                try {
                    std::string K = my_log::crypt::GenECDHSharedSecret(srv_pri2, client_pub);
                    std::string file_body = body.substr(sizeof(hdr));
                    std::printf("  [诊断] 单条：K.size()=%zu, 数据区=%zu 字节\n",
                                K.size(), file_body.size());

                    int item_count = 0;
                    bool fmt_ok = false;
                    std::string plain = DecryptChunkBody(file_body, K, item_count, fmt_ok);
                    Check(fmt_ok, "单条：数据区格式完整解析");
                    Check(item_count == 1, "单条：条目数为 1");

                    my_log::compress::ZstdCompress zc;
                    std::string decompressed = zc.Decompress(plain.data(), plain.size());
                    std::printf("  [诊断] 单条：明文=%zu, 解压=%zu 字节\n",
                                plain.size(), decompressed.size());
                    my_log::proto::EffectiveMsg em;
                    bool parsed = em.ParseFromArray(decompressed.data(),
                                                   static_cast<int>(decompressed.size()));
                    Check(parsed, "单条：protobuf 反序列化成功");
                    if (parsed) {
                        Check(em.log_info() == "hello sink test", "单条：log_info 回放正确");
                        Check(em.level() == static_cast<int32_t>(my_log::Log_Level::Error),
                              "单条：level 回放正确");
                        Check(em.line() == 5, "单条：line 回放正确");
                    }
                } catch (const std::exception& e) {
                    std::printf("  [异常] 单条：%s\n", e.what());
                    Check(false, "单条：解密/解压/反序列化未抛异常");
                }
            }
        }
    }

    std::printf("\n==== 汇总 ====\n");
    std::printf("PASS=%d  FAIL=%d\n", g_pass, g_fail);
    if (!g_failed.empty()) {
        std::printf("失败项:\n");
        for (const auto& s : g_failed) std::printf("  - %s\n", s.c_str());
    }
    return g_fail == 0 ? 0 : 1;
}
