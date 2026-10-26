// protobuf 集成验证：确认源码内嵌的 protobuf 能编、能链、protoc 生成的代码能用。
//
// 被测对象是第三方库本身（libprotobuf + protoc 代码生成），
// 消息定义在 test/proto/log_record.proto，构建时由 protoc 生成 .pb.cc/.pb.h。
//
// 用例围绕"日志落盘/传输"这个真实场景：
//   · 字段往返          → 类型、枚举、超大 timestamp 不丢精度
//   · 变长整数          → 小数值只占 1 字节（这是选 protobuf 而不是定长结构体的理由）
//   · repeated          → 批量日志一条消息装完
//   · 未知字段保留      → 老程序读到新版本写入的数据，再写回时新字段不丢（向前兼容）
//   · 截断/垃圾数据     → ParseFromString 返回 false，绝不崩
//   · TextFormat        → 调试时能直接打印成人能读的形式

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "log_record.pb.h"

#include <google/protobuf/text_format.h>

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

my_log::proto::LogRecord MakeRecord(int i) {
    my_log::proto::LogRecord r;
    r.set_timestamp_ns(1700000000000000000ULL + static_cast<uint64_t>(i));
    r.set_level(my_log::proto::LogRecord::INFO);
    r.set_thread_id(static_cast<uint32_t>(1000 + i));
    r.set_module("net");
    r.set_message("hello world " + std::to_string(i));
    return r;
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

    // ---- [1] 基本往返 ----
    std::printf("\n[1] 基本字段往返\n");
    {
        const auto in = MakeRecord(7);
        std::string wire;
        Check(in.SerializeToString(&wire), "SerializeToString 成功");

        my_log::proto::LogRecord out;
        Check(out.ParseFromString(wire), "ParseFromString 成功");
        Check(out.timestamp_ns() == in.timestamp_ns(), "uint64 时间戳不丢精度");
        Check(out.level() == my_log::proto::LogRecord::INFO, "枚举值一致");
        Check(out.thread_id() == in.thread_id(), "uint32 一致");
        Check(out.module() == "net", "字符串字段一致");
        Check(out.message() == in.message(), "消息正文一致");
        std::printf("        序列化后 %zu 字节（纯文本内容 %zu 字节）\n",
                    wire.size(), in.message().size());
    }

    // ---- [2] 默认值与空消息 ----
    std::printf("\n[2] 默认值与空消息\n");
    {
        my_log::proto::LogRecord empty;
        Check(empty.level() == my_log::proto::LogRecord::LEVEL_UNKNOWN, "未设置的枚举取零值");
        Check(empty.timestamp_ns() == 0, "未设置的整数取 0");
        Check(empty.module().empty(), "未设置的字符串为空");

        const size_t size = empty.ByteSizeLong();
        Check(size == 0, "空消息序列化体积为 0（proto3 不写默认值）");

        my_log::proto::LogRecord out;
        Check(out.ParseFromString(""), "空字节流能解析成空消息");
    }

    // ---- [3] 变长整数编码 ----
    std::printf("\n[3] 变长整数编码\n");
    {
        // 注意：变量名不能叫 small —— <windows.h> 里的 rpcndr.h 有 `#define small char`，
        // 会把声明拆成 "LogRecord char;"，报一堆莫名其妙的语法错。
        my_log::proto::LogRecord tiny;
        tiny.set_thread_id(1);
        my_log::proto::LogRecord huge;
        huge.set_thread_id(3000000000u);

        const size_t s1 = tiny.ByteSizeLong();
        const size_t s2 = huge.ByteSizeLong();
        Check(s1 == 2, "小整数（1）只占 1 字节值 + 1 字节 tag");
        Check(s2 > s1, "大整数占用更多字节");
        std::printf("        thread_id=1 → %zu 字节；thread_id=3000000000 → %zu 字节\n", s1, s2);
    }

    // ---- [4] repeated 字段 ----
    std::printf("\n[4] repeated 批量日志\n");
    {
        my_log::proto::LogBatch batch;
        batch.set_source("app.log");
        const int kCount = 100;
        for (int i = 0; i < kCount; ++i) {
            *batch.add_records() = MakeRecord(i);
        }
        Check(batch.records_size() == kCount, "add_records() 累加数量正确");

        std::string wire;
        Check(batch.SerializeToString(&wire), "批量消息序列化成功");

        my_log::proto::LogBatch back;
        Check(back.ParseFromString(wire), "批量消息反序列化成功");
        Check(back.records_size() == kCount, "反序列化后条目数一致");
        Check(back.records(99).message() == batch.records(99).message(), "最后一条内容一致");
        std::printf("        %d 条日志 → %zu 字节（平均 %zu 字节/条）\n",
                    kCount, wire.size(), wire.size() / kCount);
    }

    // ---- [5] 未知字段保留（向前兼容）----
    std::printf("\n[5] 未知字段保留\n");
    {
        std::string wire;
        MakeRecord(1).SerializeToString(&wire);

        // 手工追加一个本版本不认识的字段：tag=99, wire type=0(varint), 值=42
        // 99 << 3 | 0 = 792 → varint 编码为 0x98 0x06
        const std::string unknown_field = {'\x98', '\x06', '\x2A'};
        wire += unknown_field;

        my_log::proto::LogRecord parsed;
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
        MakeRecord(3).SerializeToString(&wire);

        const std::string truncated = wire.substr(0, wire.size() / 2);
        my_log::proto::LogRecord a;
        Check(!a.ParseFromString(truncated), "截断的字节流解析失败（而不是产出半个对象）");

        my_log::proto::LogRecord b;
        Check(!b.ParseFromString(std::string("\x00\x00\x00", 3)),
              "字段号为 0 的非法数据解析失败");

        my_log::proto::LogRecord c;
        Check(!c.ParseFromString(std::string(64, '\xFF')),
              "全 0xFF 垃圾数据解析失败");
    }

    // ---- [7] TextFormat（调试可读）----
    std::printf("\n[7] TextFormat 输出\n");
    {
        std::string text;
        Check(google::protobuf::TextFormat::PrintToString(MakeRecord(5), &text),
              "PrintToString 成功");
        Check(text.find("module: \"net\"") != std::string::npos, "文本格式含字符串字段");
        Check(text.find("level: INFO") != std::string::npos, "枚举打印成名字而不是数字");

        my_log::proto::LogRecord from_text;
        Check(google::protobuf::TextFormat::ParseFromString(text, &from_text),
              "文本格式能反解析");
        Check(from_text.message() == MakeRecord(5).message(), "文本往返内容一致");
    }

    // ---- [8] 大数据往返 ----
    std::printf("\n[8] 大数据往返\n");
    {
        my_log::proto::LogBatch batch;
        batch.set_source("big.log");
        const int kCount = 10000;
        for (int i = 0; i < kCount; ++i) {
            *batch.add_records() = MakeRecord(i);
        }

        std::string wire;
        Check(batch.SerializeToString(&wire), "1 万条日志序列化成功");

        my_log::proto::LogBatch back;
        Check(back.ParseFromString(wire), "1 万条日志反序列化成功");
        Check(back.records_size() == kCount, "条目数一致");
        Check(back.records(kCount - 1).message() == batch.records(kCount - 1).message(),
              "末条内容一致");
        std::printf("        plain≈%d 条 × 约 80 字节  →  wire %zu 字节\n",
                    kCount, wire.size());
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
