#include "logfactory.h"

my_log::LogFactory& my_log::LogFactory::GetInstance() {
    static LogFactory instance;
    return instance;
}

my_log::VariabledLogHandle* my_log::LogFactory::GetLogHandle() {
    return log_handle_.get();
}

 void my_log::LogFactory::SetLogHandle(std::shared_ptr<VariabledLogHandle> log_handle) {
    log_handle_ = log_handle;
 }