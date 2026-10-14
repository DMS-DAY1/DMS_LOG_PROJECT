#pragma once

#include <thread>

#include <mutex>

#include <condition_variable>

#include <queue>

#include <functional>

#include <vector>

#include <atomic>

#include <future>

#include <type_traits>

#include <memory>

namespace my_log {
    using Task = std::function<void()>;

    class ThreadPool {
    public:
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(size_t nums) {
            stop_.store(false);
            for (int i = 0; i < nums; i++) {
                worker_.emplace_back([this]() {
                    Work();
                });
            }

        }

        template <typename F, typename ...Args>
        void post(F&& f, Args&&... args) {
            Task task;
            task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                q_.emplace(std::move(task));
            }
            cv_.notify_one();
        }

        template <typename F, typename ...Args>
        auto RunRetTask(F&& f, Args&&... args) -> std::shared_ptr<std::future<std::invoke_result_t<F, Args...>>> {
            using rtype = std::invoke_result_t<F, Args...>;
            auto func = std::make_shared<std::packaged_task<rtype()>>(
                std::bind(std::forward<F>(f),std::forward<Args>(args)...)
            );
            {
                std::lock_guard<std::mutex> lock(mutex_);
                q_.emplace([func](){
                    (*func)();
                });
            }
            cv_.notify_one();
            auto ret = std::make_shared<std::future<rtype>>(func->get_future());
            return ret;
            
        }
        
        void Stop() {
            stop_.store(true);
            cv_.notify_all();
            for (auto& e : worker_) {
                if (e.joinable()) {
                    e.join();
                }
            }
            
        }
        ~ThreadPool() {
            Stop();
        }

    private:
        std::mutex mutex_;
        std::atomic<bool> stop_;

        std::queue<Task> q_;
        std::condition_variable cv_;

        std::vector<std::thread> worker_;
        
        void Work() {
            while (true) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock,[this]() {
                        return !q_.empty() || stop_.load();     
                    });
                    if (stop_.load() && q_.empty()) {
                        return;
                    }
                    task = std::move(q_.front());
                    q_.pop();
                }

                task();
            }
        }

    };
}