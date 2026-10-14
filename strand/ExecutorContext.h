#pragma once

#include "ThreadPool.h"

#include <unordered_map>

namespace my_log {
    using TaskRunnerTag = uint64_t;
    class ExecutorContext {
    public:
        ExecutorContext() = default;
        ~ExecutorContext() = default;

        ExecutorContext(const ExecutorContext&) = delete;
        ExecutorContext& operator=(const ExecutorContext&) = delete;

        TaskRunnerTag AddTaskRunner(const TaskRunnerTag& tag);

    private:
        using TaskRunner = ThreadPool;
        using TaskRunnerPtr = std::unique_ptr<ThreadPool>;
        friend class Executor;

        TaskRunner* GetTaskRunner(const TaskRunnerTag& tag);

        TaskRunnerTag GetNextRunnerTag(){
            return next_tag_id_++;
        }

        std::atomic<TaskRunnerTag> next_tag_id_{0};
        std::unordered_map<TaskRunnerTag, TaskRunnerPtr> task_runner_dict_;
        std::mutex mutex_;
    };
}