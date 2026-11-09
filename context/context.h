#pragma once

#include "strand/Executor.h"

namespace my_log {
    class ExecutorManager;
    class Context {
    public:
        Executor* GetExecutor();

        static Context* GetInstance() {
            static Context* instance = new Context();
            return instance;
        }

        TaskRunnerTag NewTaskRunner(TaskRunnerTag tag);

        Context(const Context&) = delete;
       Context& operator=(const Context&) = delete;
    private:
        std::unique_ptr<ExecutorManager> executor_manager_;

        Context();
    };

}

#define MYLOG_CONTEXT my_log::Context::GetInstance()

#define EXECUTOR MYLOG_CONTEXT->GetExecutor()

#define NEW_TASK_RUNNER(tag) MYLOG_CONTEXT->NewTaskRunner(tag)

#define POST_TASK(runner_tag, task) EXECUTOR->PostTask(runner_tag, task)

#define WAIT_TASK_IDLE(runner_tag) EXECUTOR->PostTaskAndGetResult(runner_tag, []() {})->wait()

#define POST_REPEATED_TASK(runner_tag, task, repeat_num, delta) \
    EXECUTOR->PostRepeatTask(runner_tag, task, repeat_num, delta)

#define POST_DELAYED_TASK(runner_tag, task, delta) \
    EXECUTOR->PostDelayedTask(runner_tag, task, delta)

#define CANCEL_REPEATED_TASK(id) EXECUTOR->CancelRepeatedTask(id)