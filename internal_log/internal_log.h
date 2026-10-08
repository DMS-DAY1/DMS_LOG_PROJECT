#pragma once

#ifdef CAN_LOG

#define FMT_HEADER_ONLY

#include "fmt/chrono.h"

#include "fmt/core.h"

#define LOG_INFO(...)                                                            \
    do {                                                                         \
        fmt::print("[INFO] [{} : {}] {} ", __FILE__, __LINE__,                   \
                   std::chrono::system_clock::now());                            \
        fmt::print(__VA_ARGS__);                                                 \
        fmt::print("\n");                                                        \
    } while (0)

#define LOG_DEBUG(...)                                                           \
    do {                                                                         \
        fmt::print("[DEBUG] [{} : {}] {} ", __FILE__, __LINE__,                  \
                   std::chrono::system_clock::now());                            \
        fmt::print(__VA_ARGS__);                                                 \
        fmt::print("\n");                                                        \
    } while (0)

#define LOG_ERROR(...)                                                           \
    do {                                                                         \
        fmt::print("[ERROR] [{} : {}] {} ", __FILE__, __LINE__,                  \
                   std::chrono::system_clock::now());                            \
        fmt::print(__VA_ARGS__);                                                 \
        fmt::print("\n");                                                        \
    } while (0)
#else

#define LOG_INFO(...)

#define LOG_DEBUG(...)

#define LOG_ERROR(...)

#endif