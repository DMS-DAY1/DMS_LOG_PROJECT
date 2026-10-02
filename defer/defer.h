#pragma once

#include <functional>



namespace defer{
    class ExecuteOnScopeExit {
    public:
        template<typename F, typename... Args>
        ExecuteOnScopeExit(F&& f, Args&&... args) {
            func_ = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        }
        ~ExecuteOnScopeExit() noexcept {
            if (func_) {
                func_();
            }
        }
        ExecuteOnScopeExit(const ExecuteOnScopeExit&) = delete;
        ExecuteOnScopeExit& operator=(const ExecuteOnScopeExit&) = delete;

        ExecuteOnScopeExit( ExecuteOnScopeExit&&) = default;
        ExecuteOnScopeExit& operator=( ExecuteOnScopeExit&&) = default;
        ExecuteOnScopeExit() = default;
    private:
        std::function<void()> func_;
    };
}

#define LOG_COMBINE(a, b) a##b

#define LOG_MK(line) defer::ExecuteOnScopeExit LOG_COMBINE(defer, line) = [&]()

#undef LOG_DEFER

#define LOG_DEFER LOG_MK(__LINE__)