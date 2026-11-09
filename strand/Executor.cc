#include "Executor.h"

my_log::TaskRunnerTag my_log::Executor::AddTaskRunner(const TaskRunnerTag& tag) {
    return executor_context_->AddTaskRunner(tag);
}

void my_log::Executor::PostTask(const TaskRunnerTag& runner_tag, Task task) {
    ExecutorContext::TaskRunner* runner = executor_context_->GetTaskRunner(runner_tag);
    runner->post(std::move(task));
}
