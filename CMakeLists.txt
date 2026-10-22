cmake_minimum_required(VERSION 3.20)
# C 也必须启用：zlib/ 与 zstd/ 都是纯 C 项目，
# 只有 CXX 的话 add_subdirectory 进去会报 “No CMAKE_C_COMPILER could be found”
project(log_client CXX C)

set(CMAKE_CXX_STANDARD 17)

# ============================================================================
# 第三方库：zlib（deflate 通用压缩）+ zstd（现代高压缩比）
# 采用源码内嵌 zlib/ 与 zstd/ 的方式（和项目自带 fmt 头文件一个思路）：
# 两边编译器用同一份源码构建，不依赖系统包管理器，也不用管 vcpkg 三元组。
# 版本：zlib 1.3.2 / zstd 1.5.6（源码内 ZLIB_VERSION / ZSTD_VERSION_STRING 可查）
# 两个库都只要静态库：不编测试、不编命令行工具、不安装。
# ============================================================================
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(zlib)          # 产出 target：zlibstatic

# 多线程支持关掉：日志压缩是单线程场景，开了反而要多链 pthread；
# LEGACY 支持关掉：不需要解压 zstd v0.1~v0.4 的古老格式，省编译体积；
# SHARED 必须关：共享库的 add_library 会引用 build/VS2010 下的 .rc 资源文件，
# 而且日志库只需要静态库（之前漏关，zstd 一直在偷偷编共享库）
set(ZSTD_BUILD_PROGRAMS       OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB        OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED         OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT  OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT       OFF CACHE BOOL "" FORCE)
add_subdirectory(zstd/build/cmake)  # 产出 target：libzstd_static

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

# logfactory + VariabledLogHandle：走 fmt 的运行时格式化路径（format_to），
# 而项目只带了 fmt 头文件（无 format.cc），所以测试文件顶部定义 FMT_HEADER_ONLY
# 并 include format-inl.h，让 fmt 的实现正常实例化（否则 extern template 会留空符号）。
# 依赖链：test → logfactory.cc + loghandle.cc（sink 用测试内的 MockSink，无需 consolesink.cc）
add_executable(test_logfactory
    test/test_logfactory.cc
    logfactory/logfactory.cc
    loghandle/loghandle.cc)

target_compile_features(test_logfactory PRIVATE cxx_std_17)

target_include_directories(test_logfactory PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_compile_options(test_logfactory PRIVATE /utf-8)
endif()

# 压缩库集成测试：链接 zlib / zstd 静态库，验证压缩-解压往返
# zlib.h / zstd.h 的 include 路径由 target 自动传播（两个库的 CMake
# 都给自己的静态库设了 INTERFACE/PUBLIC include 目录），不用手写。
# zstd 的 zconf.h 不同：zlib 的 zconf.h 是 CMake configure 生成的，
# 但传播路径里已包含生成目录，所以直接 #include "zlib.h" 就能找到它。
add_executable(test_compress
    test/test_compress.cc)

target_compile_features(test_compress PRIVATE cxx_std_17)

target_include_directories(test_compress PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/zlib
    ${PROJECT_SOURCE_DIR}/zstd/lib
)

target_link_libraries(test_compress PRIVATE
    zlibstatic
    libzstd_static
)

if(WIN32)
    target_compile_options(test_compress PRIVATE /utf-8)
endif()

# 自研压缩封装的单元测试：直接把 compress/ 下的源文件编进来
# （compress 目前还没有自己的 CMake 目标，这里就地编译，不改动源码目录）
# 和 test_compress.cc 的区别：那个测第三方库能不能用，这个测我们的封装写得对不对。
add_executable(test_mycompress
    test/test_mycompress.cc
    compress/zlib_compress.cc
    compress/zstd_compress.cc)

target_compile_features(test_mycompress PRIVATE cxx_std_17)

target_include_directories(test_mycompress PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/compress
)

target_link_libraries(test_mycompress PRIVATE
    zlibstatic
    libzstd_static
)

if(WIN32)
    target_compile_options(test_mycompress PRIVATE /utf-8)
endif()
