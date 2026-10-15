// Context（全局单例 + 门面宏）的测试
//
// 注意：Context 是全局单例，整个进程只有一份 Executor，
// 所以每个用例都要用 NEW_TASK_RUNNER 换一个新的 tag，避免互相干扰。

// 宏名已经从 CONTEXT 改成 MYLOG_CONTEXT（原来的名字和 Windows SDK 里 winnt.h 的
// CONTEXT 结构体撞车）。下面仍然保持「系统头在前、context.h 最后」的写法，
// 这是个好习惯：万一门面宏再和别人撞名，也不会污染到系统头。

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "context/context.h"

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

// 放在匿名命名空间顶层，避免 lambda 捕获 constexpr 局部变量时踩编译器的坑
constexpr int kThreads = 8;
constexpr int kPerThread = 200;

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

// 用例 1：单例 + 门面宏本身能不能用
void CaseSingleton() {
    std::cout << "[1] MYLOG_CONTEXT 单例与门面宏" << std::endl;

    my_log::Context* a = MYLOG_CONTEXT;
    my_log::Context* b = MYLOG_CONTEXT;
    Check(a == b, "MYLOG_CONTEXT 两次拿到的是同一个实例");
    Check(EXECUTOR != nullptr, "EXECUTOR 拿到的 Executor 非空");

    const auto t1 = NEW_TASK_RUNNER(1);
    const auto t2 = NEW_TASK_RUNNER(1);
    std::cout << "    两次 NEW_TASK_RUNNER(1) 得到 tag = " << t1 << " / " << t2 << std::endl;
    Check(t1 != t2, "同一个 tag 建两次会拿到两个不同通道（内部自动避让）");
}

// 用例 2：POST_TASK + WAIT_TASK_IDLE —— 用哨兵代替 sleep
void CasePostAndWaitIdle() {
    std::cout << "[2] POST_TASK + WAIT_TASK_IDLE（不靠 sleep 等）" << std::endl;
    const auto tag = NEW_TASK_RUNNER(10);

    std::atomic<int> count{0};
    POST_TASK(tag, [&]() { count.fetch_add(1); });
    POST_TASK(tag, [&]() { count.fetch_add(1); });
    POST_TASK(tag, [&]() { count.fetch_add(1); });

    WAIT_TASK_IDLE(tag);  // 空任务排到队尾，它一执行说明前面的都干完了

    std::cout << "    WAIT_TASK_IDLE 返回时 count = " << count.load() << "（期望 3）" << std::endl;
    Check(count.load() == 3, "返回时前面投的任务已全部执行完");
}

// 用例 3：WAIT_TASK_IDLE 不会提前返回（真的等到了慢任务结束）
void CaseWaitIdleBlocks() {
    std::cout << "[3] WAIT_TASK_IDLE 的等待强度" << std::endl;
    const auto tag = NEW_TASK_RUNNER(11);

    std::atomic<int> count{0};
    for (int i = 0; i < 3; ++i) {
        POST_TASK(tag, [&]() {
            std::this_thread::sleep_for(Ms(80));
            count.fetch_add(1);
        });
    }

    const auto start = Clock::now();
    WAIT_TASK_IDLE(tag);
    const long long cost = ElapsedMs(start);

    std::cout << "    3 个 80ms 任务，WAIT_TASK_IDLE 等了 " << cost << " ms（期望 >=200）" << std::endl;
    Check(count.load() == 3, "三个慢任务都执行完");
    Check(cost >= 200, "确实是阻塞等待，没有提前返回");
}

// 用例 4：同一通道内严格 FIFO（strand 的核心语义）
void CaseFifoOrder() {
    std::cout << "[4] 同一通道内的执行顺序" << std::endl;
    const auto tag = NEW_TASK_RUNNER(12);

    std::vector<int> order;  // 只在 runner 线程里写，读完前先 WAIT_TASK_IDLE
    for (int i = 0; i < 5; ++i) {
        // 注意：不能把 [&order, i] 直接写进宏参数里 —— 捕获列表的逗号会被预处理器
        // 当成宏参数分隔符。要么先存成变量，要么给整个 lambda 再套一层圆括号。
        auto job = [&order, i]() {
            std::this_thread::sleep_for(Ms(10));
            order.push_back(i);
        };
        POST_TASK(tag, job);
    }
    WAIT_TASK_IDLE(tag);

    bool ordered = (order.size() == 5);
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] != static_cast<int>(i)) {
            ordered = false;
        }
    }
    std::cout << "    执行顺序大小 = " << order.size() << std::endl;
    Check(ordered, "严格按投递顺序 0,1,2,3,4 执行");
}

// 用例 5：多线程并发往同一通道投递，普通 int 累加也精确无误（排队代替加锁）
void CaseConcurrentPost() {
    std::cout << "[5] 多线程并发投递同一通道" << std::endl;
    const auto tag = NEW_TASK_RUNNER(13);

    int counter = 0;  // 故意不用 atomic：strand 保证所有 ++ 都在同一条线程上发生

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&counter, tag]() {
            for (int i = 0; i < kPerThread; ++i) {
                POST_TASK(tag, [&counter]() { ++counter; });
            }
        });
    }
    for (auto& t : ts) {
        t.join();
    }
    WAIT_TASK_IDLE(tag);

    std::cout << "    counter = " << counter << "（期望 " << kThreads * kPerThread << "）" << std::endl;
    Check(counter == kThreads * kPerThread, "8 线程并发投递，结果精确无竞争");
}

// 用例 6：不同通道各有一条线程，互不阻塞
void CaseSeparateRunners() {
    std::cout << "[6] 不同通道互相独立（并行）" << std::endl;
    const auto tag_a = NEW_TASK_RUNNER(14);
    const auto tag_b = NEW_TASK_RUNNER(15);

    std::atomic<int> done{0};
    auto slow = [&]() {
        std::this_thread::sleep_for(Ms(100));
        done.fetch_add(1);
    };

    const auto start = Clock::now();
    for (int i = 0; i < 3; ++i) {
        POST_TASK(tag_a, slow);
        POST_TASK(tag_b, slow);
    }
    // 带超时地等：万一 strand 出问题，测试不会永远卡在这里
    while (done.load() < 6 && ElapsedMs(start) < 3000) {
        std::this_thread::sleep_for(Ms(10));
    }
    const long long cost = ElapsedMs(start);

    std::cout << "    6 个 100ms 任务总耗时 = " << cost << " ms（并行 ~300，串行 ~600）" << std::endl;
    Check(done.load() == 6, "6 个任务全部执行");
    Check(cost < 500, "两个通道并行执行（各有一条线程）");
}

// 用例 7：POST_DELAYED_TASK 延时触发
void CaseDelayed() {
    std::cout << "[7] POST_DELAYED_TASK 延时触发" << std::endl;
    const auto tag = NEW_TASK_RUNNER(16);

    const auto start = Clock::now();
    std::atomic<long long> fired_at{-1};
    POST_DELAYED_TASK(tag, [&]() { fired_at.store(ElapsedMs(start)); }, Ms(300));

    std::this_thread::sleep_for(Ms(700));
    const long long d = fired_at.load();
    std::cout << "    实际延时 = " << d << " ms（期望 ~300ms）" << std::endl;
    Check(d >= 280, "延时任务确实执行了");
    Check(d < 500, "没有明显提前触发");
}

// 用例 8：POST_REPEATED_TASK 重复执行，以及用 CANCEL_REPEATED_TASK 取消
void CaseRepeatAndCancel() {
    std::cout << "[8] POST_REPEATED_TASK 重复与取消" << std::endl;
    const auto tag = NEW_TASK_RUNNER(17);

    std::atomic<int> count{0};
    const auto id = POST_REPEATED_TASK(tag, [&]() { count.fetch_add(1); }, 100, Ms(100));

    std::this_thread::sleep_for(Ms(350));
    const int before = count.load();
    CANCEL_REPEATED_TASK(id);
    std::this_thread::sleep_for(Ms(600));

    std::cout << "    取消时 = " << before << "，之后 = " << count.load() << std::endl;
    Check(before >= 2, "取消前至少执行了 2 次");
    Check(count.load() == before, "取消后不再触发");
}

// 用例 9：往没建过的 tag 投递 —— 应自动建通道，不能崩
void CaseUnknownTag() {
    std::cout << "[9] 未注册的 tag" << std::endl;

    std::atomic<int> count{0};
    POST_TASK(9999, [&]() { count.fetch_add(1); });
    WAIT_TASK_IDLE(9999);

    Check(count.load() == 1, "未注册 tag 自动建通道并正常执行（不崩溃）");
}

// 用例 10：任意位置通过宏拿到的都是同一份全局 Executor
void CaseSameExecutorEverywhere() {
    std::cout << "[10] 全局唯一 Executor" << std::endl;
    const auto tag = NEW_TASK_RUNNER(18);

    std::atomic<int> count{0};
    {
        auto* ex = EXECUTOR;  // 假装这里在另一个 .cc 里
        ex->PostTask(tag, [&]() { count.fetch_add(1); });
    }
    WAIT_TASK_IDLE(tag);

    Check(EXECUTOR == MYLOG_CONTEXT->GetExecutor(),
          "EXECUTOR 与 MYLOG_CONTEXT->GetExecutor() 是同一个");
    Check(count.load() == 1, "任意位置投递都能落到同一个 Executor 上");
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "=== Context 测试 ===" << std::endl;
    CaseSingleton();
    CasePostAndWaitIdle();
    CaseWaitIdleBlocks();
    CaseFifoOrder();
    CaseConcurrentPost();
    CaseSeparateRunners();
    CaseDelayed();
    CaseRepeatAndCancel();
    CaseUnknownTag();
    CaseSameExecutorEverywhere();
    std::cout << "=== 结束，失败 " << g_failed << " 项 ===" << std::endl;

    // 注意：单例是 new 出来永不 delete 的，所以这里不做任何 join，
    // 进程退出时由操作系统接管残留线程——这是当前设计使然，不是 bug。
    return g_failed == 0 ? 0 : 1;
}
