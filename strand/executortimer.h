#pragma once

#include "ThreadPool.h"

#include <functional>

#include <chrono>

#include <queue>

#include <mutex>

#include <condition_variable>

#include <atomic>

#include <unordered_set>

#include <memory>

#include <thread>

namespace my_log {
    using Task = std::function<void()>;

    using RepeatTaskid = uint64_t;

    class ExecutorTimer {
    public:
        struct InternalS {
            std::chrono::time_point<std::chrono::high_resolution_clock> timept{};
            Task task;
            RepeatTaskid repeat_id;
            bool operator<(const InternalS& d) const {
                return timept > d.timept;
            }
        };

        ExecutorTimer();

        ~ExecutorTimer();

        ExecutorTimer(const ExecutorTimer&) = delete;

        ExecutorTimer& operator=(const ExecutorTimer&) = delete;

        void Stop();

        bool Start();

        void PostDelayedTask(Task task, const std::chrono::microseconds& delta);

        RepeatTaskid PostRepeatedTask(Task task, const std::chrono::microseconds& delta, uint64_t repeat_num);

        void CancelRepeatedTask(RepeatTaskid task_id);

    private:
        void Run_();

        void PostRepeatedTask_(Task task, const std::chrono::microseconds& delta,
                            RepeatTaskid repeat_task_id,
                            uint64_t repeat_num);

        void PostTask_(Task task, const std::chrono::microseconds& delta,
                    RepeatTaskid id,
                    uint64_t repeat_num);
        
        RepeatTaskid GetNextRepeatedTask_() {
            return repeated_task_id_++;
        }

        std::priority_queue<InternalS> queue_;
        std::mutex mutex_;
        std::condition_variable cond_;
        std::atomic<bool> running_;
        std::unique_ptr<ThreadPool> thread_pool_;

        std::atomic<RepeatTaskid> repeated_task_id_{0};
        std::unordered_set<RepeatTaskid> repeated_id_store_set_;
    };
}