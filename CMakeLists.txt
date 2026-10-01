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
    src)
