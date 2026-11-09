#include "ExecutorContext.h"

namespace my_log {


TaskRunnerTag ExecutorContext::AddTaskRunner(const TaskRunnerTag& tag){
    std::lock_guard<std::mutex> lock(mutex_);
    TaskRunnerTag last_tag = tag;
    while (task_runner_dict_.find(last_tag) != task_runner_dict_.end()) {
        last_tag = GetNextRunnerTag();
    }
    TaskRunnerPtr runner = std::make_unique<ThreadPool>(1);
    task_runner_dict_.emplace(last_tag, std::move(runner));
    return last_tag;
}

 ExecutorContext::TaskRunner* ExecutorContext::GetTaskRunner(const TaskRunnerTag& tag) { 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_runner_dict_.find(tag);
        if (it != task_runner_dict_.end()) {
            return it->second.get();
        }
    }
    auto ttag = AddTaskRunner(tag);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return task_runner_dict_[ttag].get();
    }

 }

}  // namespace my_log
