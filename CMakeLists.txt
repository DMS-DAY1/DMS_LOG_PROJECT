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