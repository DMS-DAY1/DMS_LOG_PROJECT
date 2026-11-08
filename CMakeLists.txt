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

# ============================================================================
# 第三方库：Crypto++ 8.9.0（数据加密）
# 和上面两个库一样走源码内嵌 cryptopp/。但 Crypto++ 官方不提供 CMakeLists，
# 那份 cryptopp/CMakeLists.txt 是我们自己写的（规则照抄官方 GNUmakefile）。
# 产出 target：cryptopp（也有别名 CryptoPP::CryptoPP）
# 头文件通过 target 的 PUBLIC include 目录传播，调用方写 #include "cryptopp/aes.h"
# ============================================================================
add_subdirectory(cryptopp)

# ============================================================================
# 第三方库：protobuf 21.12（结构化序列化）
# 同上，源码内嵌 protobuf/。选 21.12 而不是 v22+ 的原因：从 v22 起 protobuf
# 强依赖 abseil（要再接十几个子库），21.12 是最后一个自包含版本。
# 产出三个 target：libprotobuf（运行时）、libprotoc（编译器库）、protoc（可执行）
#
# 各选项的含义：
#   BUILD_TESTS/CONFORMANCE/EXAMPLES OFF —— 不编测试、一致性测试、示例（省一半编译量）
#   BUILD_PROTOC_BINARIES ON —— 必须编 protoc，否则 .proto 没东西生成代码
#   BUILD_SHARED_LIBS OFF —— 只要静态库，和 zlib/zstd 保持一致
#   WITH_ZLIB OFF —— 不用 protobuf 的 gzip 流，别让它去 find_package(ZLIB) 撞上系统库
#   MSVC_STATIC_RUNTIME OFF —— 这一项最要紧：默认 ON 会给 protobuf 加 /MT，
#     而本项目其余目标都是 /MD，混用会在链接期报 LNK2038（RuntimeLibrary 不匹配）
# ============================================================================
set(protobuf_BUILD_TESTS           OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE     OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON  CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL               OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB             OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME   OFF CACHE BOOL "" FORCE)
add_subdirectory(protobuf)

# fmt 只带头文件：编译期统一给 FMT_HEADER_ONLY，保证任何 TU 里 fmt 头首次展开就走 header-only。
# 用空值以兼容源码中已有的 #define FMT_HEADER_ONLY，避免 C4005 宏重定义。
add_compile_definitions(FMT_HEADER_ONLY=)

add_executable(test_mmap
    test/test_mmap.cc)

# 源文件级条件编译：Windows 用 mmap_win.cc，Linux 用 mmap_linux.cc
if(WIN32)
    target_sources(test_mmap PRIVATE mmap/mmap_win.cc)
    # MSVC 需要它才能正确解析 UTF-8 源码（gcc/clang 没有这个选项）
    target_compile_options(test_mmap PRIVATE /utf-8)
else()
    target_sources(test_mmap PRIVATE mmap/mmap_linux.cc)
endif()

target_compile_features(test_mmap PRIVATE cxx_std_17)

target_include_directories(test_mmap PRIVATE
    ${PROJECT_SOURCE_DIR}
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

# Crypto++ 集成验证：链接 cryptopp 静态库，确认 AES-GCM / CBC / SHA-256 能用。
# 头文件路径完全靠 target 传播（cryptopp 目标设了 PUBLIC 的父目录），
# 所以这里没有手写 cryptopp 的 include 路径——写 #include "cryptopp/aes.h" 即可。
add_executable(test_cryptopp
    test/test_cryptopp.cc)

target_compile_features(test_cryptopp PRIVATE cxx_std_17)

target_include_directories(test_cryptopp PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(test_cryptopp PRIVATE
    cryptopp
)

if(WIN32)
    target_compile_options(test_cryptopp PRIVATE /utf-8)
endif()

# 自研加密封装的单元测试：直接把 crypt/ 下的源文件编进来
# （crypt 目前还没有自己的 CMake 目标，这里就地编译，不改动源码目录）
# 和 test_cryptopp.cc 的区别：那个测第三方库能不能用，这个测我们的封装写得对不对。
add_executable(test_mycrypt
    test/test_mycrypt.cc
    crypt/crypt.cc
    crypt/aes_crypt.cc)

target_compile_features(test_mycrypt PRIVATE cxx_std_17)

target_include_directories(test_mycrypt PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(test_mycrypt PRIVATE
    cryptopp
)

if(WIN32)
    target_compile_options(test_mycrypt PRIVATE /utf-8)
endif()

# protobuf 集成验证：protoc 生成代码 + 序列化往返
# 生成文件放进 build 目录（不污染源码树），.proto 改动后自动重新生成。
# $<TARGET_FILE:protoc> 而不是写死路径：多配置生成器下 protoc.exe 在
# build/protobuf/Release/ 或 Debug/ 下，取决于当前构建配置，写死会找错。
set(PB_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/proto)
set(PB_GEN_DIR   ${CMAKE_CURRENT_BINARY_DIR}/pbgen)
file(MAKE_DIRECTORY ${PB_GEN_DIR})

add_custom_command(
    OUTPUT  ${PB_GEN_DIR}/effective_msg.pb.cc
            ${PB_GEN_DIR}/effective_msg.pb.h
    COMMAND protoc
            --proto_path=${PB_PROTO_DIR}
            --cpp_out=${PB_GEN_DIR}
            ${PB_PROTO_DIR}/effective_msg.proto
    DEPENDS ${PB_PROTO_DIR}/effective_msg.proto
    COMMENT "protoc: effective_msg.proto -> effective_msg.pb.{h,cc}"
    VERBATIM)

add_executable(test_protobuf
    test/test_protobuf.cc
    ${PB_GEN_DIR}/effective_msg.pb.cc)

target_compile_features(test_protobuf PRIVATE cxx_std_17)

# libprotobuf 已经把 protobuf/src 作为 PUBLIC include 目录传播过来了，
# 所以 <google/protobuf/...> 不用手写；这里只需要加生成目录（effective_msg.pb.h 在 build 里）
target_include_directories(test_protobuf PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PB_GEN_DIR}
)

target_link_libraries(test_protobuf PRIVATE
    libprotobuf
)

if(WIN32)
    target_compile_options(test_protobuf PRIVATE /utf-8)
endif()

# EffectiveFormatter 单元测试：验证 LogMsg -> EffectiveMsg 序列化
# 把 formatter/effectiveformatter.cc 编进测试（这是它第一次真正进入构建，
# 之前它是孤儿文件，从未被编译器验证过）。依赖链：
#   test -> effectiveformatter.cc + sys_util.cc(.win) + effective_msg.pb.cc
add_executable(test_effectiveformatter
    test/test_effectiveformatter.cc
    formatter/effectiveformatter.cc
    utils/sys_util.cc
    ${PB_GEN_DIR}/effective_msg.pb.cc)

target_compile_features(test_effectiveformatter PRIVATE cxx_std_17)

target_include_directories(test_effectiveformatter PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PB_GEN_DIR}
)

target_link_libraries(test_effectiveformatter PRIVATE
    libprotobuf
)

if(WIN32)
    target_sources(test_effectiveformatter PRIVATE utils/sys_util_win.cc)
    target_compile_options(test_effectiveformatter PRIVATE /utf-8)
endif()

# EffectiveSink 端到端测试：验证 构造 -> 写日志 -> zstd 压缩 -> AES 加密
# -> 双缓冲 mmap 落盘 -> 读回文件头 -> 服务器私钥协商 -> 解密 -> 解压 -> 反序列化。
# 依赖链：test -> effectivesink.cc + effectiveformatter.cc
#   + context(单例) + strand(Executor / ExecutorContext / executortimer)
#   + crypt(crypt.cc, aes_crypt.cc) + compress(zstd_compress.cc)
#   + utils(sys_util, file_util) + effective_msg.pb.cc
add_executable(test_effectivesink
    test/test_effectivesink.cc
    sink/effectivesink.cc
    formatter/effectiveformatter.cc
    context/context.cc
    strand/Executor.cc
    strand/ExecutorContext.cc
    strand/executortimer.cc
    crypt/crypt.cc
    crypt/aes_crypt.cc
    compress/zstd_compress.cc
    utils/sys_util.cc
    utils/file_util.cc
    ${PB_GEN_DIR}/effective_msg.pb.cc)

target_compile_features(test_effectivesink PRIVATE cxx_std_17)

# 注意：sink/effectivesink.h 及其依赖头目前混用两种 include 风格——
#   * 带前缀： #include "log_msg/log_msg.h"（依赖项目根）
#   * 裸文件名：#include "space.h" / "crypt.h"（依赖各自所在目录）
# 因此这里把相关兄弟目录一并加入搜索路径，保证裸名 include 可解析。
target_include_directories(test_effectivesink PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/sink
    ${PROJECT_SOURCE_DIR}/space
    ${PROJECT_SOURCE_DIR}/context
    ${PROJECT_SOURCE_DIR}/crypt
    ${PROJECT_SOURCE_DIR}/mmap
    ${PROJECT_SOURCE_DIR}/compress
    ${PROJECT_SOURCE_DIR}/formatter
    ${PROJECT_SOURCE_DIR}/log_msg
    ${PROJECT_SOURCE_DIR}/strand
    ${PROJECT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/internal_log
    ${PB_GEN_DIR}
)

target_link_libraries(test_effectivesink PRIVATE
    libzstd_static
    cryptopp
    libprotobuf
)

if(WIN32)
    target_sources(test_effectivesink PRIVATE
        utils/sys_util_win.cc
        mmap/mmap_win.cc)
    target_compile_options(test_effectivesink PRIVATE /utf-8)
    target_compile_definitions(test_effectivesink PRIVATE CAN_LOG)
else()
    target_sources(test_effectivesink PRIVATE
        mmap/mmap_linux.cc)
endif()

# ============================================================================
# decode：日志解析命令行工具
#   decode.cc 自带 main()，所以是独立可执行目标。此前 decode/ 没进任何 target，
#   IDE 拿不到 compile_commands 条目、找不到 build/pbgen 下的 effective_msg.pb.h，
#   这就是 decode_formatter.h 整片爆红的根因。
# ============================================================================
add_executable(decode
    decode/decode.cc
    decode/decode_formatter.cc
    crypt/crypt.cc
    crypt/aes_crypt.cc
    compress/zstd_compress.cc
    ${PB_GEN_DIR}/effective_msg.pb.cc)

target_compile_features(decode PRIVATE cxx_std_17)

# decode/ 里混用了裸文件名 include（"space.h" / <crypt.h> / "zstd_compress.h"）
# 和带前缀 include（"log_msg/log_msg.h"），两类路径都要给。
set(DECODE_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/decode
    ${PROJECT_SOURCE_DIR}/sink
    ${PROJECT_SOURCE_DIR}/space
    ${PROJECT_SOURCE_DIR}/context
    ${PROJECT_SOURCE_DIR}/crypt
    ${PROJECT_SOURCE_DIR}/mmap
    ${PROJECT_SOURCE_DIR}/compress
    ${PROJECT_SOURCE_DIR}/formatter
    ${PROJECT_SOURCE_DIR}/log_msg
    ${PROJECT_SOURCE_DIR}/strand
    ${PROJECT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/internal_log
    ${PB_GEN_DIR}
)

target_include_directories(decode PRIVATE ${DECODE_INCLUDE_DIRS})
target_link_libraries(decode PRIVATE
    libzstd_static
    cryptopp
    libprotobuf
)

if(WIN32)
    target_compile_options(decode PRIVATE /utf-8)
    target_compile_definitions(decode PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ============================================================================
# test_decode：decode 模块端到端测试
#   test/test_decode.cc 里用宏把 decode.cc 的 main 改名后直接 include 进来，
#   以便复用 DecodeFile / DecodeChunkData，因此这里【不要】再列 decode/decode.cc，
#   否则会重复定义。
# ============================================================================
add_executable(test_decode
    test/test_decode.cc
    decode/decode_formatter.cc
    crypt/crypt.cc
    crypt/aes_crypt.cc
    compress/zstd_compress.cc
    ${PB_GEN_DIR}/effective_msg.pb.cc)

target_compile_features(test_decode PRIVATE cxx_std_17)
target_include_directories(test_decode PRIVATE ${DECODE_INCLUDE_DIRS})
target_link_libraries(test_decode PRIVATE
    libzstd_static
    cryptopp
    libprotobuf
)

if(WIN32)
    target_compile_options(test_decode PRIVATE /utf-8)
    target_compile_definitions(test_decode PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ============================================================================
# test_my_log：对外头 my_log/my_log.h 的自动化回归。
#   依赖链与 test_logfactory 相同（sink 用测试内的 RecordingSink，无需 consolesink.cc）。
#   test_my_log_fmt_order.cc 故意先 include fmt/format.h，用来守住第 67 行的
#   add_compile_definitions(FMT_HEADER_ONLY=)：删掉它，这个 TU 立刻 LNK2019。
# ============================================================================
add_executable(test_my_log
    test/test_my_log.cc
    test/test_my_log_fmt_order.cc
    logfactory/logfactory.cc
    loghandle/loghandle.cc)

target_compile_features(test_my_log PRIVATE cxx_std_17)

target_include_directories(test_my_log PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(WIN32)
    target_compile_options(test_my_log PRIVATE /utf-8)
endif()
