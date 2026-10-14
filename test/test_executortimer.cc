#include "strand/executortimer.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
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

// 用例 1：延时任务能按时触发（大致准时、不提前）
void CaseBasicDelayed() {
    std::cout << "[1] 延时任务基本触发" << std::endl;
    my_log::ExecutorTimer timer;
    timer.Start();

    const auto start = Clock::now();
    std::atomic<long long> fired_at{-1};
    timer.PostDelayedTask([&]() { fired_at.store(ElapsedMs(start)); }, Ms(300));

    std::this_thread::sleep_for(Ms(700));
    const long long d = fired_at.load();
    std::cout << "    实际延时 = " << d << " ms（期望 ~300ms）" << std::endl;
    Check(d >= 280, "任务确实执行了");
    Check(d < 450, "没有明显提前触发");
}

// 用例 2：多个任务严格按截止时刻从小到大执行（priority_queue 排序正确）
void CaseOrder() {
    std::cout << "[2] 多个任务按截止时刻排序" << std::endl;
    my_log::ExecutorTimer timer;
    timer.Start();

    std::mutex m;
    std::vector<int> order;
    auto make = [&](int id) {
        return [&, id]() {
            std::lock_guard<std::mutex> lk(m);
            order.push_back(id);
        };
    };

    timer.PostDelayedTask(make(1), Ms(400));
    timer.PostDelayedTask(make(2), Ms(100));
    timer.PostDelayedTask(make(3), Ms(250));

    std::this_thread::sleep_for(Ms(800));
    Check(order.size() == 3, "三个任务全部执行");
    Check(order == std::vector<int>({2, 3, 1}), "顺序为 2(100ms) → 3(250ms) → 1(400ms)");
}

// 用例 3：更近的任务插队提前执行（验证 Run_ 里 wait_for 的快照谓词）
// 若谓词失效，B 会被 A 拖到 2000ms 才执行
void CaseInsertCloser() {
    std::cout << "[3] 更近的任务插队提前执行" << std::endl;
    my_log::ExecutorTimer timer;
    timer.Start();

    const auto start = Clock::now();
    std::atomic<long long> a_at{-1};
    std::atomic<long long> b_at{-1};

    timer.PostDelayedTask([&]() { a_at.store(ElapsedMs(start)); }, Ms(2000));
    std::this_thread::sleep_for(Ms(200));
    timer.PostDelayedTask([&]() { b_at.store(ElapsedMs(start)); }, Ms(400));

    std::this_thread::sleep_for(Ms(2400));
    std::cout << "    A(2000ms) 实际 = " << a_at.load()
              << " ms，B(400ms) 实际 = " << b_at.load() << " ms" << std::endl;
    Check(a_at.load() >= 1950 && a_at.load() < 2300, "A 在 ~2000ms 执行");
    Check(b_at.load() >= 550 && b_at.load() < 1000, "B 在 ~600ms 执行（没被 A 拖到 2000ms）");
}

// 用例 4：重复任务执行指定次数后链条终止
// 注意：PostRepeatedTask 的第 1 次是立即在调用方线程执行的
void CaseRepeated() {
    std::cout << "[4] 重复任务执行指定次数后停止" << std::endl;
    my_log::ExecutorTimer timer;
    timer.Start();

    std::atomic<int> count{0};
    std::chrono::microseconds delta = Ms(100);
    timer.PostRepeatedTask([&]() { count.fetch_add(1); }, delta, 3);

    std::this_thread::sleep_for(Ms(900));
    std::cout << "    执行次数 = " << count.load() << "（期望 3）" << std::endl;
    Check(count.load() == 3, "正好执行 3 次后终止");
}

// 用例 5：取消重复任务后不再触发
void CaseCancel() {
    std::cout << "[5] 取消重复任务" << std::endl;
    my_log::ExecutorTimer timer;
    timer.Start();

    std::atomic<int> count{0};
    std::chrono::microseconds delta = Ms(100);
    auto id = timer.PostRepeatedTask([&]() { count.fetch_add(1); }, delta, 100);

    std::this_thread::sleep_for(Ms(250));
    const int before = count.load();
    timer.CancelRepeatedTask(id);
    std::this_thread::sleep_for(Ms(600));

    std::cout << "    取消时次数 = " << before << "，之后次数 = " << count.load() << std::endl;
    Check(before >= 2, "取消前至少执行了 2 次");
    Check(count.load() == before, "取消后不再触发");
}

// 用例 6：停止时"不丢弃"剩余任务 —— 立即把队列里剩下的执行掉，且析构不阻塞
// 期望三个：秒退、任务没被丢、任务是立刻跑的（不是干等到 10 秒后）
void CaseStopDrains() {
    std::cout << "[6] 停止时执行完剩余任务，且析构不阻塞" << std::endl;
    const auto start = Clock::now();
    std::atomic<long long> fired_at{-1};
    {
        my_log::ExecutorTimer timer;
        timer.Start();
        timer.PostDelayedTask([&]() { fired_at.store(ElapsedMs(start)); }, Ms(10000));
        std::this_thread::sleep_for(Ms(100));
    }  // 离开作用域即析构 → Stop() → Run_ drain 后退出
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

    std::cout << "=== ExecutorTimer 测试 ===" << std::endl;
    CaseBasicDelayed();
    CaseOrder();
    CaseInsertCloser();
    CaseRepeated();
    CaseCancel();
    CaseStopDrains();
    std::cout << "=== 结束，失败 " << g_failed << " 项 ===" << std::endl;

    return g_failed == 0 ? 0 : 1;
}
