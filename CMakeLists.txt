cmake_minimum_required(VERSION 3.20)
project(log_client CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(test_mmap
    test/test_mmap.cc)

# 源文件级条件编译：Windows 用 mmap_win.cc，Linux 用 mmap_linux.cc
if(WIN32)
    target_sources(test_mmap PRIVATE src/mmap/mmap_win.cc)
    # MSVC 需要它才能正确解析 UTF-8 源码（gcc/clang 没有这个选项）
    target_compile_options(test_mmap PRIVATE /utf-8)
else()
    target_sources(test_mmap PRIVATE src/mmap/mmap_linux.cc)
endif()

target_compile_features(test_mmap PRIVATE cxx_std_17)

target_include_directories(test_mmap PRIVATE
    src
)

add_executable(test_defer test/test_defer.cc)

target_compile_features(test_defer PRIVATE cxx_std_17)

target_include_directories(test_defer PRIVATE
    ${PROJECT_SOURCE_DIR}
)

add_executable(test_space test/test_space.cc)

target_compile_features(test_space PRIVATE cxx_std_17)

target_include_directories(test_space PRIVATE
    ${PROJECT_SOURCE_DIR}
)

add_executable(test_fmt test/test_fmt.cc)

target_compile_features(test_fmt PRIVATE cxx_std_17)

target_include_directories(test_fmt PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# MSVC 需要它才能正确解析 UTF-8 源码；同时 fmt 会在编译期探测源字符集是否为 UTF-8
# （见 fmt/core.h 里的 is_utf8_enabled），没有这个选项会直接 static_assert 失败
if(WIN32)
    target_compile_options(test_fmt PRIVATE /utf-8)
endif()

add_executable(test_log_msg test/test_log_msg.cc)

target_compile_features(test_log_msg PRIVATE cxx_std_17)

target_include_directories(test_log_msg PRIVATE
    ${PROJECT_SOURCE_DIR}
)

add_executable(test_internal_log test/test_internal_log.cc)

target_compile_features(test_internal_log PRIVATE cxx_std_17)

target_include_directories(test_internal_log PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 打开日志开关：统一由构建系统传宏，调用方 .cpp 不用自己 #define
target_compile_definitions(test_internal_log PRIVATE CAN_LOG)

# 目标必须先 add_executable 创建，target_* 命令才能生效（顺序写反会直接报错）
# MSVC 需要它才能正确解析 UTF-8 源码；fmt 会在编译期探测源字符集是否为 UTF-8
if(WIN32)
    target_compile_options(test_internal_log PRIVATE /utf-8)
endif()

add_executable(test_thread_pool test/test_thread_pool.cc)

target_compile_features(test_thread_pool PRIVATE cxx_std_17)

target_include_directories(test_thread_pool PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 定时器依赖线程池，所以要把 executortimer.cc 一起编进来（ThreadPool.h 是 header-only，不用编）
add_executable(test_executortimer
    test/test_executortimer.cc
    strand/executortimer.cc)

target_compile_features(test_executortimer PRIVATE cxx_std_17)

target_include_directories(test_executortimer PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_compile_options(test_executortimer PRIVATE /utf-8)
endif()

# Executor 依赖三条链：ExecutorContext（.cc）+ ExecutorTimer（.cc）+ ThreadPool（header-only，不用编）
add_executable(test_executor
    test/test_executor.cc
    strand/Executor.cc
    strand/ExecutorContext.cc
    strand/executortimer.cc)

target_compile_features(test_executor PRIVATE cxx_std_17)

target_include_directories(test_executor PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_compile_options(test_executor PRIVATE /utf-8)
endif()

# Context 是全局门面，依赖链：context.cc → Executor.cc → ExecutorContext.cc / executortimer.cc
# （ThreadPool.h 是 header-only，不用编进来）
add_executable(test_context
    test/test_context.cc
    context/context.cc
    strand/Executor.cc
    strand/ExecutorContext.cc
    strand/executortimer.cc)

target_compile_features(test_context PRIVATE cxx_std_17)

target_include_directories(test_context PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_compile_options(test_context PRIVATE /utf-8)
endif()

# utils：平台适配层 + 文件工具。sys_util 按平台二选一（同 src/mmap 的模式），
# file_util.cc / sys_util.cc 平台无关，两个平台都编。
# 注意：utils 的 .cc 里写的是 #include "utils/sys_util.h"（从项目根算），
# 所以必须像其他 target 一样把 ${PROJECT_SOURCE_DIR} 加进 include 路径。
add_executable(test_utils
    test/test_utils.cc
    utils/file_util.cc
    utils/sys_util.cc)

target_compile_features(test_utils PRIVATE cxx_std_17)

target_include_directories(test_utils PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_sources(test_utils PRIVATE utils/sys_util_win.cc)
    # MSVC 需要它才能正确解析 UTF-8 源码（测试文件里有中文注释）
    target_compile_options(test_utils PRIVATE /utf-8)
else()
    target_sources(test_utils PRIVATE utils/sys_util_linux.cc)
endif()

# formatter + sink：DefaultFormatter 依赖 sys_util（时间/pid/tid），
# ConsoleSink 依赖 DefaultFormatter（默认构造时用），sink.h 里 LogSink 是抽象基类
add_executable(test_format_sink
    test/test_format_sink.cc
    formatter/defaultformatter.cc
    sink/consolesink.cc
    utils/sys_util.cc)

target_compile_features(test_format_sink PRIVATE cxx_std_17)

target_include_directories(test_format_sink PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_sources(test_format_sink PRIVATE utils/sys_util_win.cc)
    target_compile_options(test_format_sink PRIVATE /utf-8)
else()
    target_sources(test_format_sink PRIVATE utils/sys_util_linux.cc)
endif()