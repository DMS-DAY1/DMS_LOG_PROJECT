// ============================================================================
// test_decode.cc —— decode 模块端到端测试
//
// 覆盖：
//   1. 按 EffectiveSink 真实落盘格式生成 .log（多 chunk / 多 item）
//   2. DecodeFile 全链路：ChunkHeader → ECDH → AES 帧 → zstd → protobuf → 格式化
//   3. 错误输入：错误私钥 / 坏 magic / 文件过小
//   4. DecodeFormatter 各 pattern flag 的单元验证
//
// 说明：decode/decode.cc 自带 main()，测试需要复用它里面的 DecodeFile /
// DecodeChunkData / DecodeItemData，但不能再链接一个 main。因此这里用宏把它的
// main 改名后直接包含进本翻译单元（decode 的解析逻辑一行未改）。
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "aes_crypt.h"
#include "crypt.h"
#include "effective_msg.pb.h"
#include "effectivesink.h"
#include "zstd_compress.h"

#include "decode_formatter.h"

#define main decode_cli_main
#include "decode/decode.cc"
#undef main

// 不能起别名 fs：decode.cc 里 using namespace my_log，而 utils/file_util.h
// 有 my_log::fs，会让 fs 变成歧义符号（C2872）。
namespace dsk = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const std::string& name) {
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s\n", name.c_str());
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", name.c_str());
    }
}

static dsk::path g_tmpdir;

// decode.cc 里的两个全局对象由它的 main 初始化；测试里需要自己装配。
static void ResetGlobals(const std::string& pattern = "[%l][%S][%p:%t][%F:%f:%#]%v") {
    decode_formatter = std::make_unique<DecodeFormatter>();
    decode_formatter->SetPattern(pattern);
    decompress = std::make_unique<my_log::compress::ZstdCompress>();
}

static std::string ReadText(const dsk::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return {};
    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

// 当前实现里 Format 和 DecodeChunkData 各加一个换行，因此日志行之间夹空行。
// 过滤空行后逐条比对，避免把这个已知行为误当成失败。
static std::vector<std::string> NonEmptyLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t e = s.find('\n', start);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(start, e - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
        start = e + 1;
    }
    return out;
}

struct GenOpts {
    int chunks = 2;
    int items = 3;
    bool corrupt_chunk_magic = false;
    bool corrupt_item_magic = false;
    bool tiny = false;
};

struct GenResult {
    std::string srv_pri_hex;   // 传给 decode 的服务器私钥（hex）
    dsk::path log;
    size_t size = 0;
    std::vector<std::string> infos;
    std::vector<int> lines;
    std::vector<std::string> secs;
};

// 复刻 EffectiveSink 的写入格式：
//   [ChunkHeader 144B] 后面 N 组 [ItemHeader 16B][AES 帧]
//   每个 chunk 开头 ResetStream()，逐条 ZSTD_e_flush 压缩
static GenResult GenerateLog(const std::string& tag, const GenOpts& opt) {
    auto [svr_pri, svr_pub] = my_log::crypt::GenECDHKey();
    auto [cli_pri, cli_pub] = my_log::crypt::GenECDHKey();
    std::string K = my_log::crypt::GenECDHSharedSecret(cli_pri, svr_pub);

    GenResult r;
    r.srv_pri_hex = my_log::crypt::BinaryKeyToHex(svr_pri);
    r.log = g_tmpdir / (tag + ".log");

    my_log::compress::ZstdCompress comp;
    my_log::crypt::AESCrypt enc(K);

    std::ofstream ofs(r.log, std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("GenerateLog: open output failed");

    for (int c = 0; c < opt.chunks; ++c) {
        std::string data;
        for (int i = 0; i < opt.items; ++i) {
            my_log::proto::EffectiveMsg m;
            m.set_level(4);  // Error -> "E"
            int line = c * 10 + i + 1;
            m.set_line(line);
            m.set_pid(1234);
            m.set_tid(5678);
            auto ts = 1730000000000LL + c * 1000 + i;
            m.set_timestamp(ts);
            m.set_file_name("main.cc");
            m.set_func_name("foo");

            std::string info = "chunk " + std::to_string(c) + " item " + std::to_string(i);
            // 最后一条用中文，验证 UTF-8 正文能完整还原
            if (c == opt.chunks - 1 && i == opt.items - 1) info = "中文日志-解码测试";
            m.set_log_info(info);

            r.infos.push_back(info);
            r.lines.push_back(line);
            r.secs.push_back(std::to_string(ts / 1000));

            std::string plain = m.SerializeAsString();
            if (i == 0) comp.ResetStream();
            std::vector<char> buf(comp.CompressedBound(plain.size()));
            size_t n = comp.Compress(plain.data(), plain.size(), buf.data(), buf.size());

            std::string frame;
            enc.Encrypt(buf.data(), n, frame);

            my_log::detail::ItemHeader item;
            item.size = frame.size();
            if (opt.corrupt_item_magic && c == 0 && i == 0) item.magic = 0;
            data.append(reinterpret_cast<const char*>(&item), sizeof(item));
            data.append(frame);
        }

        my_log::detail::ChunkHeader chunk;
        chunk.size = data.size();
        std::memcpy(chunk.pub_key, cli_pub.data(), cli_pub.size());
        if (opt.corrupt_chunk_magic && c == 0) chunk.magic = 0;
        ofs.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));
        ofs.write(data.data(), data.size());
    }
    ofs.close();

    if (opt.tiny) {
        std::ofstream tiny(r.log, std::ios::binary | std::ios::trunc);
        tiny.write("x", 1);
    }
    r.size = dsk::file_size(r.log);
    return r;
}

static size_t FindSecondChunkOffset(const dsk::path& log) {
    std::string body = ReadText(log);
    const size_t hdr = sizeof(my_log::detail::ChunkHeader);
    if (body.size() < hdr) return std::string::npos;
    auto* first = reinterpret_cast<const my_log::detail::ChunkHeader*>(body.data());
    return hdr + static_cast<size_t>(first->size);
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::printf("==== decode 模块端到端测试 ====\n");

    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    g_tmpdir = dsk::temp_directory_path() / ("decode_test_" + std::to_string(stamp));
    std::error_code ec;
    dsk::create_directories(g_tmpdir, ec);

    // ------------------------------------------------------------------ 用例 0
    // flag_formatters_ 是全局的，初始为空时 Format 走 CombinelogMsg 分支。
    // 必须在第一次 SetPattern 之前测，否则全局已被填充，测不到默认格式。
    std::printf("\n[用例 0] DecodeFormatter 默认格式（尚未设置任何 pattern）\n");
    my_log::proto::EffectiveMsg default_msg;
    default_msg.set_level(4);
    default_msg.set_timestamp(1730000000000LL);
    default_msg.set_pid(1234);
    default_msg.set_tid(5678);
    default_msg.set_line(42);
    default_msg.set_file_name("main.cc");
    default_msg.set_func_name("foo");
    default_msg.set_log_info("hello");
    DecodeFormatter default_fmt;
    std::string default_out;
    default_fmt.Format(default_msg, default_out);
    Check(default_out.find("[4][1730000000000][1234:5678][main.cc:foo:42]hello") != std::string::npos,
          "未设 pattern 时走 CombinelogMsg 默认格式");

    // ------------------------------------------------------------------ 用例 1
    std::printf("\n[用例 1] 落盘格式生成（2 chunk x 3 item）\n");
    GenResult g;
    bool gen_ok = true;
    try {
        g = GenerateLog("multi", GenOpts{});
    } catch (const std::exception& e) {
        std::printf("  [异常] %s\n", e.what());
        gen_ok = false;
    }
    Check(gen_ok, "生成日志文件未抛异常");
    Check(dsk::exists(g.log), "日志文件已创建");
    Check(g.size > 2 * sizeof(my_log::detail::ChunkHeader), "文件大小超过两个 ChunkHeader");

    std::string body = ReadText(g.log);
    const size_t HDR = sizeof(my_log::detail::ChunkHeader);
    const size_t ITEM = sizeof(my_log::detail::ItemHeader);
    Check(HDR == 144 && ITEM == 16, "ChunkHeader=144B / ItemHeader=16B");

    auto* h1 = reinterpret_cast<const my_log::detail::ChunkHeader*>(body.data());
    Check(h1->magic == my_log::detail::ChunkHeader::kmagic, "第 1 个 ChunkHeader magic 正确");
    Check(h1->size > 0 && HDR + h1->size < body.size(), "第 1 个 chunk 数据区长度合理");

    size_t off2 = FindSecondChunkOffset(g.log);
    auto* h2 = reinterpret_cast<const my_log::detail::ChunkHeader*>(body.data() + off2);
    Check(off2 + HDR <= body.size() && h2->magic == my_log::detail::ChunkHeader::kmagic,
          "第 2 个 ChunkHeader magic 正确（多 chunk 追加布局）");
    Check(h2->size > 0 && off2 + HDR + h2->size == body.size(),
          "第 2 个 chunk 数据区正好延伸到文件尾");
    Check(std::string(h1->pub_key, 65) == std::string(h2->pub_key, 65),
          "同一 sink 各 chunk 的客户端公钥一致");

    auto* it1 = reinterpret_cast<const my_log::detail::ItemHeader*>(body.data() + HDR);
    Check(it1->magic == my_log::detail::ItemHeader::kmagic, "第 1 个 ItemHeader magic 正确");
    Check(it1->size > 0 && it1->size < h1->size, "第 1 个 ItemHeader.size 在 chunk 范围内");

    // ------------------------------------------------------------------ 用例 2
    std::printf("\n[用例 2] DecodeFile 全链路（2 chunk x 3 item）\n");
    dsk::path out = g_tmpdir / "multi.txt";
    dsk::remove(out, ec);
    ResetGlobals();
    bool decode_ok = true;
    try {
        DecodeFile(g.log.string(), g.srv_pri_hex, out.string());
    } catch (const std::exception& e) {
        std::printf("  [异常] %s\n", e.what());
        decode_ok = false;
    }
    Check(decode_ok, "DecodeFile 未抛异常（ECDH + 解密 + 解压 + 格式化全通）");
    Check(dsk::exists(out), "输出文件已生成");

    std::string text = ReadText(out);
    Check(!text.empty(), "输出文件非空");
    auto lines = NonEmptyLines(text);
    std::printf("  [诊断] 输出 %zu 字节，非空行 %zu 行（Format 与 DecodeChunkData 各加换行，故夹空行）\n",
                text.size(), lines.size());
    Check(lines.size() == g.infos.size(), "还原出 6 条日志（跨 2 个 chunk）");

    if (lines.size() == g.infos.size()) {
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            Check(line.find(g.infos[i]) != std::string::npos,
                  "第 " + std::to_string(i + 1) + " 条正文还原正确：" + g.infos[i]);
            Check(line.find("[E]") != std::string::npos,
                  "第 " + std::to_string(i + 1) + " 条 level=E");
            Check(line.find("[" + g.secs[i] + "]") != std::string::npos,
                  "第 " + std::to_string(i + 1) + " 条 epoch 秒正确");
            Check(line.find("[1234:5678]") != std::string::npos,
                  "第 " + std::to_string(i + 1) + " 条 pid:tid 正确");
            Check(line.find("[main.cc:foo:" + std::to_string(g.lines[i]) + "]") != std::string::npos,
                  "第 " + std::to_string(i + 1) + " 条 file:func:line 正确");
        }
    }
    Check(text.find("中文日志-解码测试") != std::string::npos, "中文正文完整还原（UTF-8）");

    // ------------------------------------------------------------------ 用例 3
    std::printf("\n[用例 3] 单 chunk 单条日志\n");
    GenOpts one_opt;
    one_opt.chunks = 1;
    one_opt.items = 1;
    GenResult one = GenerateLog("single", one_opt);
    dsk::path out_one = g_tmpdir / "single.txt";
    dsk::remove(out_one, ec);
    ResetGlobals();
    bool one_ok = true;
    try {
        DecodeFile(one.log.string(), one.srv_pri_hex, out_one.string());
    } catch (const std::exception& e) {
        std::printf("  [异常] %s\n", e.what());
        one_ok = false;
    }
    Check(one_ok, "单 chunk 单条 DecodeFile 成功");
    auto one_lines = NonEmptyLines(ReadText(out_one));
    Check(one_lines.size() == 1, "输出恰好 1 条日志");
    if (one_lines.size() == 1) {
        Check(one_lines[0].find(one.infos[0]) != std::string::npos, "单条正文正确");
        Check(one_lines[0].find("[E]") != std::string::npos, "单条 level 正确");
    }

    // ------------------------------------------------------------------ 用例 4
    std::printf("\n[用例 4] 错误输入应被拒绝\n");
    auto expect_throw = [&](const std::string& name, const dsk::path& log, const std::string& key) {
        dsk::path o = g_tmpdir / "err.txt";
        dsk::remove(o, ec);
        ResetGlobals();
        bool threw = false;
        try {
            DecodeFile(log.string(), key, o.string());
        } catch (const std::exception&) {
            threw = true;
        }
        Check(threw, name);
    };

    // 另一把无关的私钥
    auto [wrong_pri, wrong_pub] = my_log::crypt::GenECDHKey();
    std::string wrong_hex = my_log::crypt::BinaryKeyToHex(wrong_pri);
    expect_throw("错误服务器私钥：解密失败并抛异常", g.log, wrong_hex);

    GenOpts bad_chunk;
    bad_chunk.corrupt_chunk_magic = true;
    GenResult bc = GenerateLog("bad_chunk", bad_chunk);
    expect_throw("损坏的 ChunkHeader magic：抛异常", bc.log, bc.srv_pri_hex);

    GenOpts bad_item;
    bad_item.corrupt_item_magic = true;
    GenResult bi = GenerateLog("bad_item", bad_item);
    expect_throw("损坏的 ItemHeader magic：抛异常", bi.log, bi.srv_pri_hex);

    GenOpts tiny_opt;
    tiny_opt.tiny = true;
    GenResult tn = GenerateLog("tiny", tiny_opt);
    expect_throw("文件过小（< ChunkHeader）：抛异常", tn.log, tn.srv_pri_hex);

    // ------------------------------------------------------------------ 用例 5
    std::printf("\n[用例 5] DecodeFormatter pattern 单元验证\n");
    ResetGlobals();
    my_log::proto::EffectiveMsg m;
    m.set_level(4);
    m.set_timestamp(1730000000000LL);
    m.set_pid(1234);
    m.set_tid(5678);
    m.set_line(42);
    m.set_file_name("main.cc");
    m.set_func_name("foo");
    m.set_log_info("hello");

    // flag_formatters_ 是全局变量，所有 DecodeFormatter 实例共享同一份 pattern。
    // 这里如实记录当前设计（默认格式已在用例 0 覆盖，此处不再重复测）。
    DecodeFormatter shared;
    std::string shared_out;
    shared.Format(m, shared_out);
    Check(shared_out.find("[E][1730000000][1234:5678][main.cc:foo:42]hello") != std::string::npos,
          "新建实例沿用全局 pattern（flag_formatters_ 为全局，已知设计）");

    auto fmt = [&](const char* pattern) {
        DecodeFormatter f;
        f.SetPattern(pattern);
        std::string s;
        f.Format(m, s);
        return s;
    };

    Check(fmt("%l").find("E") != std::string::npos, "%l -> level 字符串");
    Check(fmt("%S").find("1730000000") != std::string::npos, "%S -> epoch 秒");
    Check(fmt("%M").find("1730000000000") != std::string::npos, "%M -> 毫秒");
    Check(fmt("%p").find("1234") != std::string::npos, "%p -> pid");
    Check(fmt("%t").find("5678") != std::string::npos, "%t -> tid");
    Check(fmt("%#").find("42") != std::string::npos, "%# -> 行号");
    Check(fmt("%F").find("main.cc") != std::string::npos, "%F -> 文件名");
    Check(fmt("%f").find("foo") != std::string::npos, "%f -> 函数名");
    Check(fmt("%v").find("hello") != std::string::npos, "%v -> 正文");
    Check(fmt("%D").size() >= 12, "%D -> 本地日期时间非空");
    Check(fmt("[%z]").find("[%z]") != std::string::npos, "未知 flag 原样保留");
    Check(fmt("[%l][%S]%v").find("[E][1730000000]hello") != std::string::npos,
          "组合 pattern 按序拼接");

    // 重复 SetPattern 不应累积格式器
    DecodeFormatter twice;
    twice.SetPattern("%v");
    twice.SetPattern("%v");
    std::string twice_out;
    twice.Format(m, twice_out);
    Check(twice_out == "hello\n", "重复 SetPattern 不累积（输出不重复）");

    // level 0~5 与越界值
    const char* expect_level[] = {"V", "D", "I", "W", "E", "F", "U"};
    bool level_all_ok = true;
    for (int lv = 0; lv <= 6; ++lv) {
        m.set_level(lv);
        if (fmt("%l").find(expect_level[lv]) == std::string::npos) level_all_ok = false;
    }
    Check(level_all_ok, "level 0~6 映射 V/D/I/W/E/F/U 全部正确");

    // ------------------------------------------------------------------ 汇总
    std::printf("\n==== 汇总：PASS=%d  FAIL=%d ====\n", g_pass, g_fail);
    dsk::remove_all(g_tmpdir, ec);
    return g_fail == 0 ? 0 : 1;
}
