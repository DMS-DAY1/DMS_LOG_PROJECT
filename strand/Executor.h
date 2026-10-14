#pragma once

#include "ExecutorContext.h"

#include "executortimer.h"

namespace my_log {
    using Task = std::function<void(void)>;
    using TaskRunnerTag = uint64_t;

    class Executor {
    public:
        Executor() : executor_context_(std::make_unique<ExecutorContext>()), executor_timer_(std::make_unique<ExecutorTimer>()) {};
        ~Executor() = default;
        Executor(const Executor&) = delete;
        Executor& operator=(const Executor&) = delete;

        TaskRunnerTag AddTaskRunner(const TaskRunnerTag& tag);

        void PostTask(const TaskRunnerTag& runner_tag, Task task);

        template <typename R, typename P>
        void PostDelayedTask(const TaskRunnerTag& runner_tag, Task task, const std::chrono::duration<R,P>& delta) {
            Task func = std::bind(&Executor::PostTask, this, runner_tag, std::move(task));
            executor_timer_->Start();
            executor_timer_->PostDelayedTask(std::move(func), delta);
        }

        template <typename P, typename R>
        RepeatTaskid PostRepeatTask(const TaskRunnerTag& runner_tag, Task task, uint64_t repeat_num, const std::chrono::duration<R,P>& delta) {
            Task func = std::bind(&Executor::PostTask, this, runner_tag, std::move(task));
            executor_timer_->Start();
            return executor_timer_->PostRepeatedTask(std::move(func), std::chrono::duration_cast<std::chrono::microseconds> (delta), repeat_num);
        }

        template <typename F, typename... Args>
        auto PostTaskAndGetResult(const TaskRunnerTag& runner_tag,
                                 F&& f,
                                 Args&&... args) -> std::shared_ptr<std::future<std::invoke_result_t<F,Args...>>> {
            ExecutorContext::TaskRunner* task_runner = executor_context_->GetTaskRunner(runner_tag);
            auto ret = task_runner->RunRetTask(std::forward<F>(f), std::forward<Args>(args)...);
            return ret;
        }

        void CancelRepeatedTask(RepeatTaskid task_id) {
            executor_timer_->CancelRepeatedTask(task_id);
        }
        
    

    private:
        std::unique_ptr<ExecutorContext> executor_context_;
        std::unique_ptr<ExecutorTimer> executor_timer_;
    };
}