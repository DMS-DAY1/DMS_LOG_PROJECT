// protobuf 集成验证：确认源码内嵌的 protobuf 能编、能链、protoc 生成的代码能用。
//
// 被测对象是第三方库本身（libprotobuf + protoc 代码生成），
// 消息定义在 test/proto/effective_msg.proto（DMS 自己写的日志消息），
// 构建时由 protoc 生成 .pb.cc/.pb.h，命名空间 my_log::proto::。
//
// 注意：effective_msg.proto 里有 `option optimize_for = LITE_RUNTIME;`，
// 生成的是 lite 版消息（派生自 MessageLite，不带反射/描述符），所以：
//   · 不支持 TextFormat —— 它需要反射，这里不能用
//   · 序列化 / 反序列化 / ByteSizeLong 都正常
//
// 用例围绕"日志落盘/传输"这个真实场景：
//   · 字段往返          → 所有类型不丢精度
//   · 变长整数          → 小数值只占 1 字节（这是选 protobuf 而不是定长结构体的理由）
//   · 默认值            → proto3 不写默认值，空消息体积为 0
//   · 超长正文          → 几万字节的日志内容完整往返
//   · 未知字段保留      → 老程序读到新版本写入的数据，再写回时新字段不丢（向前兼容）
//   · 截断/垃圾数据     → ParseFromString 返回 false，绝不崩
//   · 长度前缀分帧      → 模拟真实落盘格式，1 万条全部还原

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "effective_msg.pb.h"

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

my_log::proto::EffectiveMsg MakeMsg(int i) {
    my_log::proto::EffectiveMsg m;
    m.set_level(i % 6 + 1);
    m.set_timestamp(1700000000000000000LL + static_cast<int64_t>(i));
    m.set_pid(4321);
    m.set_tid(static_cast<int32_t>(1000 + i));
    m.set_line(static_cast<int32_t>(42 + i));
    m.set_file_name("logger.cc");
    m.set_func_name("Write");
    m.set_log_info("hello world " + std::to_string(i));
    return m;
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 校验头文件与运行时库版本一致，不一致会直接 abort（protobuf 官方推荐的第一件事）
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    std::printf("protobuf version: %s\n",
                google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION).c_str());

    // ---- [1] 基本字段往返 ----
    std::printf("\n[1] 基本字段往返\n");
    {
        const auto in = MakeMsg(7);
        std::string wire;
        Check(in.SerializeToString(&wire), "SerializeToString 成功");

        my_log::proto::EffectiveMsg out;
        Check(out.ParseFromString(wire), "ParseFromString 成功");
        Check(out.level() == in.level(), "int32 level 一致");
        Check(out.timestamp() == in.timestamp(), "int64 timestamp 不丢精度");
        Check(out.pid() == in.pid(), "int32 pid 一致");
        Check(out.tid() == in.tid(), "int32 tid 一致");
        Check(out.line() == in.line(), "int32 line 一致");
        Check(out.file_name() == in.file_name(), "string file_name 一致");
        Check(out.func_name() == in.func_name(), "string func_name 一致");
        Check(out.log_info() == in.log_info(), "string log_info 一致");
        std::printf("        序列化后 %zu 字节（纯文本内容 %zu 字节）\n",
                    wire.size(), in.log_info().size());
    }

    // ---- [2] 默认值与空消息 ----
    std::printf("\n[2] 默认值与空消息\n");
    {
        my_log::proto::EffectiveMsg empty;
        Check(empty.level() == 0, "未设置的 int32 取 0");
        Check(empty.timestamp() == 0, "未设置的 int64 取 0");
        Check(empty.file_name().empty(), "未设置的字符串为空");
        Check(empty.log_info().empty(), "未设置的正文为空");

        const size_t size = empty.ByteSizeLong();
        Check(size == 0, "空消息序列化体积为 0（proto3 不写默认值）");

        my_log::proto::EffectiveMsg out;
        Check(out.ParseFromString(""), "空字节流能解析成空消息");
    }

    // ---- [3] 变长整数编码 ----
    std::printf("\n[3] 变长整数编码\n");
    {
        // 注意：变量名不能叫 small —— <windows.h> 里的 rpcndr.h 有 `#define small char`，
        // 会把声明拆成 "EffectiveMsg char;"，报一堆莫名其妙的语法错。
        my_log::proto::EffectiveMsg tiny;
        tiny.set_tid(1);
        my_log::proto::EffectiveMsg huge;
        huge.set_tid(2000000000);

        const size_t s1 = tiny.ByteSizeLong();
        const size_t s2 = huge.ByteSizeLong();
        Check(s1 == 2, "小整数（1）只占 1 字节值 + 1 字节 tag");
        Check(s2 > s1, "大整数占用更多字节");
        std::printf("        tid=1 → %zu 字节；tid=2000000000 → %zu 字节\n", s1, s2);
    }

    // ---- [4] 超长日志正文 ----
    std::printf("\n[4] 超长日志正文\n");
    {
        my_log::proto::EffectiveMsg m;
        m.set_level(5);
        m.set_log_info(std::string(60000, 'x'));

        std::string wire;
        Check(m.SerializeToString(&wire), "6 万字节正文序列化成功");

        my_log::proto::EffectiveMsg back;
        Check(back.ParseFromString(wire), "6 万字节正文反序列化成功");
        Check(back.log_info().size() == 60000, "正文长度完整");
        Check(back.log_info() == m.log_info(), "正文内容一致");
    }

    // ---- [5] 未知字段保留（向前兼容）----
    std::printf("\n[5] 未知字段保留\n");
    {
        std::string wire;
        MakeMsg(1).SerializeToString(&wire);

        // 手工追加一个本版本不认识的字段：tag=99, wire type=0(varint), 值=42
        // 99 << 3 | 0 = 792 → varint 编码为 0x98 0x06
        const std::string unknown_field = {'\x98', '\x06', '\x2A'};
        wire += unknown_field;

        my_log::proto::EffectiveMsg parsed;
        Check(parsed.ParseFromString(wire), "含未知字段的字节流能解析");

        std::string reserialized;
        parsed.SerializeToString(&reserialized);
        Check(reserialized.find(unknown_field) != std::string::npos,
              "再序列化时未知字段仍在（老程序回写不丢新字段）");
        std::printf("        原始 %zu 字节 → 回写 %zu 字节\n", wire.size(), reserialized.size());
    }

    // ---- [6] 损坏数据不能崩 ----
    std::printf("\n[6] 损坏数据鲁棒性\n");
    {
        std::string wire;
        MakeMsg(3).SerializeToString(&wire);

        const std::string truncated = wire.substr(0, wire.size() / 2);
        my_log::proto::EffectiveMsg a;
        Check(!a.ParseFromString(truncated), "截断的字节流解析失败（而不是产出半个对象）");

        my_log::proto::EffectiveMsg b;
        Check(!b.ParseFromString(std::string("\x00\x00\x00", 3)),
              "字段号为 0 的非法数据解析失败");

        my_log::proto::EffectiveMsg c;
        Check(!c.ParseFromString(std::string(64, '\xFF')),
              "全 0xFF 垃圾数据解析失败");
    }

    // ---- [7] 长度前缀分帧：模拟真实落盘格式 ----
    std::printf("\n[7] 长度前缀分帧（1 万条）\n");
    {
        // EffectiveMsg 没有 repeated 字段，真实落盘一般自己分帧：
        // [4 字节大端长度][消息体] 依次拼接。这也顺带验证了
        // "一次 ParseFromString 只吃一条消息"的分帧必要性。
        const int kCount = 10000;
        std::string stream;
        for (int i = 0; i < kCount; ++i) {
            std::string one;
            MakeMsg(i).SerializeToString(&one);
            const uint32_t len = static_cast<uint32_t>(one.size());
            const char len_bytes[4] = {
                static_cast<char>((len >> 24) & 0xFF),
                static_cast<char>((len >> 16) & 0xFF),
                static_cast<char>((len >>  8) & 0xFF),
                static_cast<char>((len      ) & 0xFF),
            };
            stream.append(len_bytes, 4);
            stream += one;
        }

        size_t pos = 0;
        int ok_count = 0;
        while (pos + 4 <= stream.size()) {
            const uint32_t len =
                (static_cast<uint32_t>(static_cast<unsigned char>(stream[pos]))     << 24) |
                (static_cast<uint32_t>(static_cast<unsigned char>(stream[pos + 1])) << 16) |
                (static_cast<uint32_t>(static_cast<unsigned char>(stream[pos + 2])) <<  8) |
                (static_cast<uint32_t>(static_cast<unsigned char>(stream[pos + 3])));
            pos += 4;
            if (len == 0 || pos + len > stream.size()) break;

            my_log::proto::EffectiveMsg m;
            if (m.ParseFromString(stream.substr(pos, len))) {
                if (m.log_info() == "hello world " + std::to_string(ok_count)) ++ok_count;
            }
            pos += len;
        }
        Check(ok_count == kCount, "1 万条按长度前缀分帧后全部正确还原");
        std::printf("        1 万条 → wire %zu 字节（平均 %zu 字节/条）\n",
                    stream.size(), stream.size() / kCount);
    }

    std::printf("\n========================================\n");
    std::printf("结果：%d 项 PASS，%d 项 FAIL\n", g_pass, g_fail);
    if (!g_failed.empty()) {
        std::printf("失败项：\n");
        for (const auto& s : g_failed) {
            std::printf("  - %s\n", s.c_str());
        }
    }
    std::printf("========================================\n");
    return g_fail == 0 ? 0 : 1;
}
