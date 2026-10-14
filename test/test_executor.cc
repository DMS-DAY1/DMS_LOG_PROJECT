#include "strand/Executor.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

int g_failed = 0;

void Check(bool ok, const char* what) {
    std::cout << (ok ? "    [PASS] " : "    [FAIL] ") << what << std::endl;
    if (!ok) {
        ++g_failed;
    }
}

long long ElapsedMs(const Clock::time_point& since) {
    return std::chrono::duration_cast<Ms>(Clock::now() - since).count();
}

// 用例 1：AddTaskRunner + PostTask —— 任务确实被放到 runner 上执行
void CasePostTask() {
    std::cout << "[1] PostTask 基本执行" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    std::atomic<int> count{0};
    ex.PostTask(tag, [&]() { count.fetch_add(1); });
    ex.PostTask(tag, [&]() { count.fetch_add(1); });

    std::this_thread::sleep_for(Ms(300));
    std::cout << "    执行次数 = " << count.load() << "（期望 2）" << std::endl;
    Check(count.load() == 2, "两个任务都被执行");
}

// 用例 2：PostTaskAndGetResult —— 多参数 + 有返回值
void CaseResultValue() {
    std::cout << "[2] PostTaskAndGetResult 取返回值" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    auto fi = ex.PostTaskAndGetResult(tag, [](int a, int b) { return a + b; }, 3, 4);
    auto fs = ex.PostTaskAndGetResult(tag, [](const char* s) { return std::string(s) + "!"; }, "hi");

    const int sum = fi->get();
    const std::string str = fs->get();
    std::cout << "    3+4 = " << sum << "，字符串 = " << str << std::endl;
    Check(sum == 7, "int 返回值正确（验证 invoke_result_t<F, Args...>）");
    Check(str == "hi!", "std::string 返回值正确");
}

// 用例 3：PostTaskAndGetResult —— void 返回值
// 若 invoke_result_t 写成 <F(Args...)>，Args 为空时会被推导成 Lambda 类型本身，这里直接编不过
void CaseResultVoid() {
    std::cout << "[3] PostTaskAndGetResult 返回 void" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    std::atomic<int> count{0};
    auto fv = ex.PostTaskAndGetResult(tag, [&]() { count.fetch_add(1); });
    fv->get();

    Check(count.load() == 1, "void 任务执行且 future 正常就绪");
}

// 用例 4：任务抛异常 —— 应经由 future 传出，由 get() 重新抛出（不能把 runner 线程打死）
void CaseResultThrow() {
    std::cout << "[4] 任务抛异常时的传递" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    auto f = ex.PostTaskAndGetResult(tag, []() -> int {
        throw std::runtime_error("boom");
    });

    bool caught = false;
    try {
        f->get();
    } catch (const std::exception& e) {
        caught = true;
        std::cout << "    捕获到： " << e.what() << std::endl;
    }
    Check(caught, "异常由 future 传出，get() 重新抛出");

    // 线程还活着吗：再发一个普通任务
    std::atomic<int> alive{0};
    ex.PostTask(tag, [&]() { alive.fetch_add(1); });
    std::this_thread::sleep_for(Ms(200));
    Check(alive.load() == 1, "抛异常后 runner 线程仍然存活");
}

// 用例 5：PostTask 到未注册的 tag —— GetTaskRunner 会自动建一个 runner，不应崩溃
void CaseUnknownTag() {
    std::cout << "[5] 未注册的 tag" << std::endl;
    my_log::Executor ex;

    std::atomic<int> count{0};
    ex.PostTask(9999, [&]() { count.fetch_add(1); });

    std::this_thread::sleep_for(Ms(300));
    std::cout << "    执行次数 = " << count.load() << std::endl;
    Check(count.load() == 1, "未注册 tag 会自动创建 runner 并正常执行（不崩溃）");
}

// 用例 6：PostDelayedTask —— 延时后在指定时刻触发
void CaseDelayed() {
    std::cout << "[6] PostDelayedTask 延时触发" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    const auto start = Clock::now();
    std::atomic<long long> fired_at{-1};
    ex.PostDelayedTask(tag, [&]() { fired_at.store(ElapsedMs(start)); }, Ms(300));

    std::this_thread::sleep_for(Ms(700));
    const long long d = fired_at.load();
    std::cout << "    实际延时 = " << d << " ms（期望 ~300ms）" << std::endl;
    Check(d >= 280, "延时任务确实执行了");
    Check(d < 500, "没有明显提前触发");
}

// 用例 7：PostRepeatTask —— 重复指定次数后链条终止
// 注意：第 1 次是 PostRepeatedTask_ 里立即同步执行的，之后每 delta 一次
void CaseRepeat() {
    std::cout << "[7] PostRepeatTask 重复指定次数" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    std::atomic<int> count{0};
    ex.PostRepeatTask(tag, [&]() { count.fetch_add(1); }, 3, Ms(100));

    std::this_thread::sleep_for(Ms(900));
    std::cout << "    执行次数 = " << count.load() << "（期望 3）" << std::endl;
    Check(count.load() == 3, "正好执行 3 次后终止");
}

// 用例 8：CancelRepeatedTask —— 取消后不再触发
void CaseCancelRepeat() {
    std::cout << "[8] CancelRepeatedTask 取消重复任务" << std::endl;
    my_log::Executor ex;
    const auto tag = ex.AddTaskRunner(1);

    std::atomic<int> count{0};
    const auto id = ex.PostRepeatTask(tag, [&]() { count.fetch_add(1); }, 100, Ms(100));

    std::this_thread::sleep_for(Ms(350));
    const int before = count.load();
    ex.CancelRepeatedTask(id);
    std::this_thread::sleep_for(Ms(600));

    std::cout << "    取消时 = " << before << "，之后 = " << count.load() << std::endl;
    Check(before >= 2, "取消前至少执行了 2 次");
    Check(count.load() == before, "取消后不再触发");
}

// 用例 9：strand 语义 —— 不同 tag 各有独立线程，互不阻塞
// 两个 tag 各 3 个 100ms 任务：并行应 ~300ms，若共用一个 runner 则要 ~600ms
void CaseSeparateRunners() {
    std::cout << "[9] 不同 tag 的 runner 互相独立" << std::endl;
    my_log::Executor ex;
    const auto tag_a = ex.AddTaskRunner(1);
    const auto tag_b = ex.AddTaskRunner(2);

    std::atomic<int> done{0};
    auto slow = [&]() {
        std::this_thread::sleep_for(Ms(100));
        done.fetch_add(1);
    };

    const auto start = Clock::now();
    for (int i = 0; i < 3; ++i) {
        ex.PostTask(tag_a, slow);
        ex.PostTask(tag_b, slow);
    }

    while (done.load() < 6) {
        std::this_thread::sleep_for(Ms(10));
    }
    const long long cost = ElapsedMs(start);
    std::cout << "    6 个 100ms 任务总耗时 = " << cost << " ms（并行 ~300，串行 ~600）" << std::endl;
    Check(done.load() == 6, "6 个任务全部执行");
    Check(cost < 500, "两个 tag 并行执行（各自一条线程）");
}

// 用例 10：析构不阻塞 —— 排了远期任务后立刻析构，应毫秒级返回且不丢活
void CaseDestroyFast() {
    std::cout << "[10] 析构时执行完剩余任务且不阻塞" << std::endl;
    const auto start = Clock::now();
    std::atomic<long long> fired_at{-1};
    {
        my_log::Executor ex;
        const auto tag = ex.AddTaskRunner(1);
        ex.PostDelayedTask(tag, [&]() { fired_at.store(ElapsedMs(start)); }, Ms(10000));
        std::this_thread::sleep_for(Ms(100));
    }  // 离开作用域即析构 → 定时器 Stop() → drain 剩余任务 → runner 线程 join
    const long long cost = ElapsedMs(start);

    std::cout << "    析构耗时 = " << cost << " ms，任务在第 " << fired_at.load()
              << " ms 执行" << std::endl;
    Check(cost < 1000, "析构毫秒级返回（没有干等 10 秒）");
    Check(fired_at.load() > 0, "剩余任务没有被丢弃");
    Check(fired_at.load() < 1000, "任务是立刻执行的，不是等到 10 秒后");
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "=== Executor 测试 ===" << std::endl;
    CasePostTask();
    CaseResultValue();
    CaseResultVoid();
    CaseResultThrow();
    CaseUnknownTag();
    CaseDelayed();
    CaseRepeat();
    CaseCancelRepeat();
    CaseSeparateRunners();
    CaseDestroyFast();
    std::cout << "=== 结束，失败 " << g_failed << " 项 ===" << std::endl;

    return g_failed == 0 ? 0 : 1;
}
