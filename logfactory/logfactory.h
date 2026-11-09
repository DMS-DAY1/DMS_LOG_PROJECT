#pragma once

#include <memory>

namespace my_log {
    class VariabledLogHandle;
    
    class LogFactory {
    public:
        LogFactory(const LogFactory&) = delete;
        LogFactory& operator=(const LogFactory&) = delete;

        static LogFactory& GetInstance();

        VariabledLogHandle* GetLogHandle();

        void SetLogHandle(std::shared_ptr<VariabledLogHandle> log_handle);
    private:
        LogFactory() = default;

        std::shared_ptr<VariabledLogHandle> log_handle_;
    };
}