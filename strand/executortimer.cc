#include "executortimer.h"

#include <vector>

namespace my_log {


ExecutorTimer::ExecutorTimer() {
    thread_pool_ = std::make_unique<ThreadPool>(1);
    repeated_task_id_.store(0);
    running_.store(false);
}

ExecutorTimer::~ExecutorTimer() {
    Stop();
}

void ExecutorTimer::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false);
        cond_.notify_all();
    }
    thread_pool_.reset();
}

bool ExecutorTimer::Start() {
    if (running_) {
        return true;
    }
    running_.store(true);
    thread_pool_->post(&ExecutorTimer::Run_, this);
    return true;
}

void ExecutorTimer::Run_() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock,[this] () {
            return !queue_.empty() || !running_;
        });
        if (queue_.empty() && !running_) {
            return;
        }
        
        if (!running_) {
            std::vector<InternalS> rest;
            while (!queue_.empty()) {         
                rest.push_back(queue_.top());
                queue_.pop();
            }
            lock.unlock();                      
            for (auto& s : rest) {
                s.task();
            }
            return;
        }
        auto s = queue_.top();
        auto diff = s.timept - std::chrono::high_resolution_clock::now();
        auto st = s.timept;
        if (diff.count() > 0) {
            cond_.wait_for(lock, diff,[this, st](){
                return  !running_ || queue_.top().timept < st;
            });
            continue;
        }
        queue_.pop();
        lock.unlock();
        s.task();
    }
}

void ExecutorTimer::PostDelayedTask(Task task, const std::chrono::microseconds& dela) {
    InternalS s;
    s.timept = std::chrono::high_resolution_clock::now() + dela;
    s.task = std::move(task);
    {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(s);
    }
    cond_.notify_all();
}

RepeatTaskid ExecutorTimer::PostRepeatedTask(Task task, const std::chrono::microseconds& delta, uint64_t repeat_num) {
    RepeatTaskid id = GetNextRepeatedTask_();
    std::unique_lock<std::mutex> lock(mutex_);
    repeated_id_store_set_.insert(id);
    lock.unlock();
    PostRepeatedTask_(std::move(task), delta, id, repeat_num);
    return id;
}

void ExecutorTimer::PostRepeatedTask_(Task task, const std::chrono::microseconds& delta,RepeatTaskid id, uint64_t repeat_num) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (repeated_id_store_set_.find(id) == repeated_id_store_set_.end() || repeat_num == 0) {
        return;
    }
    lock.unlock();
    task();
    
    Task func = std::bind(&ExecutorTimer::PostTask_, this, std::move(task), delta, id, repeat_num - 1);
    InternalS s;
    s.timept = std::chrono::high_resolution_clock::now() + delta;
    s.repeat_id = id;
    s.task = std::move(func);

    {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(s); 
    }
    cond_.notify_all();
}

void ExecutorTimer::PostTask_(Task task, const std::chrono::microseconds& delta, RepeatTaskid id, uint64_t repeat_num) {
    PostRepeatedTask_(task, delta, id, repeat_num);
}

void ExecutorTimer::CancelRepeatedTask(RepeatTaskid task_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    repeated_id_store_set_.erase(task_id);
}

}  // namespace my_log
