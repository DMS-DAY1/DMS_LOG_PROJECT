# ============================================================================
# Crypto++ 8.9.0 —— 源码内嵌（和 zlib/ / zstd/ 一个思路）
#
# 官方只提供 GNUmakefile 和 VS 工程，**不带 CMakeLists.txt**，
# 所以这份是本项目自己写的，规则照抄官方 GNUmakefile 第 1187 行：
#     SRCS := cryptlib.cpp cpu.cpp integer.cpp
#             $(filter-out cryptlib.cpp cpu.cpp integer.cpp pch.cpp simple.cpp,
#                          $(sort $(wildcard *.cpp)))
#
# 三条约定：
#   1. 只编静态库（对应官方的 libcryptopp.a），不编 cryptest / dlltest 之类
#   2. 关掉手写汇编（见下方 CRYPTOPP_DISABLE_ASM 的说明）
#   3. 静音所有警告——第三方库的警告不是你的错，也不该混进你的构建日志
# ============================================================================

# 只编库本体，不需要 install / 测试
set(CMAKE_SKIP_INSTALL_RULES ON)

file(GLOB CRYPTOPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 排除：测试程序、基准测试、验证套件、官方也不编的两个文件
# （validat*.cpp / bench*.cpp / test.cpp 是 cryptest 可执行程序的组成部分；
#   pch.cpp 只是预编译头的桩；simple.cpp 是示例实现，官方明确 filter-out）
set(_exclude
    bench1 bench2 bench3
    datatest dlltest
    fipstest fipsalgt
    regtest1 regtest2 regtest3 regtest4
    test
    validat0 validat1 validat2 validat3 validat4
    validat5 validat6 validat7 validat8 validat9 validat10
    pch simple
)

foreach(_n IN LISTS _exclude)
    list(FILTER CRYPTOPP_SOURCES EXCLUDE REGEX "/${_n}\\.cpp$")
endforeach()

add_library(cryptopp STATIC ${CRYPTOPP_SOURCES})
add_library(CryptoPP::CryptoPP ALIAS cryptopp)

# ----------------------------------------------------------------------------
# MSVC + x64：编官方的 x64dll.asm（MASM）
#
# 为什么必须编它：Crypto++ 在 Windows 上的 SIMD 加速是**手写 MASM 汇编**实现的
# （GCC 那边才用内联汇编）。cpu.cpp 里的 CPU 探测函数 XGETBV64 / CPUID64，
# 以及 AES 的 Rijndael_Enc_AdvancedProcessBlocks_SSE2、GCM 的
# GCM_AuthenticateBlocks_2K_SSE2、SHA 的 SHA256_HashMultipleBlocks_SSE2、
# integer 的 Baseline_Add —— 全都定义在这个文件里。
# 少编它，链接阶段会报 6 个 LNK2019。
#
# 只编 x64dll.asm 就够了，官方 vcxproj 里那个 x64masm.asm 是另一批算法
# （Salsa20 等），本项目用不到。
# ----------------------------------------------------------------------------
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM_MASM)

    # x64dll.asm 开头有一句 `include ksamd64.inc`，ml64 需要能找到它。
    # 这个文件在 Windows SDK 的 Include/<版本>/shared/ 下，版本目录不固定，所以要搜。
    file(GLOB _ksamd_candidates
         "C:/Program Files (x86)/Windows Kits/10/Include/*/shared/ksamd64.inc")
    if(_ksamd_candidates)
        # 可能有多个 SDK 版本，取排序后的最后一个（版本号最大的那个）
        list(SORT _ksamd_candidates)
        list(GET _ksamd_candidates -1 _ksamd_file)
        get_filename_component(_ksamd_dir "${_ksamd_file}" DIRECTORY)
        message(STATUS "Crypto++ MASM: found ksamd64.inc in ${_ksamd_dir}")
    else()
        set(_ksamd_dir "")
        message(WARNING "Crypto++ MASM: 没找到 ksamd64.inc，x64dll.asm 可能汇编失败")
    endif()

    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/x64dll.asm"
        PROPERTIES COMPILE_OPTIONS "/D_M_X64 /W3 /I${_ksamd_dir}")

    target_sources(cryptopp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/x64dll.asm")
endif()

# 关键：只把**父目录**（也就是项目根）作为 PUBLIC 的 include 路径。
# 这样调用方写的是 #include "cryptopp/aes.h"，路径里带着库名，不会和
# 项目自己的头文件撞名。
#
# 为什么不把 cryptopp/ 本身也暴露出去：Crypto++ 有一堆极通用的头文件名
# （queue.h / misc.h / config.h / files.h ...），一旦进全局搜索路径，
# 哪天你自己的项目里出现同名文件就会打架。
#
# Crypto++ 头文件之间互相引用用的是 #include "aes.h" 这种同目录形式，
# 引号 include 天然先搜当前文件所在目录，所以不加当前目录它自己也认得路。
target_include_directories(cryptopp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# 注意：**不要**定义 CRYPTOPP_DISABLE_ASM。
#
# 很多人遇到 Crypto++ 在新编译器上编不过，第一反应就是加这个宏。但它关掉的不只是
# 手写汇编——config_asm.h 里 SSE2 / SSSE3 / AES-NI / AVX / SHA 等**所有**硬件加速
# 的探测宏，全都包在 `#if !defined(CRYPTOPP_DISABLE_ASM)` 里。一禁用，AES 就从
# AES-NI 指令退化成纯软件查表，性能差一个数量级，对加密模块是实打实的损失。
#
# 那 x64dll.asm 里那两个函数怎么办？见同目录的 msvc_x64_cpu_shim.cpp——
# 用 MSVC 的 _xgetbv / __cpuidex intrinsic 补上，不动第三方源码，也不用配 MASM。
#
# CRYPTOPP_NO_STDEXT_ITERATOR：本项目加的逃生开关（不是 Crypto++ 官方的宏）。
#
# Crypto++ 8.9 里有两处用了 stdext::make_checked_array_iterator /
# stdext::make_unchecked_array_iterator（integer.cpp、zdeflate.cpp）。
# 这是 MSVC 的老扩展，VS2022 17.10 之后被彻底移除，而官方源码里的判断
# 只看 CRYPTOPP_MSC_VERSION >= 1500/1600，于是新编译器必然编不过。
#
# 我们给那两个 #if 各补了一个 !defined(CRYPTOPP_NO_STDEXT_ITERATOR)，
# 定义本宏即退回纯标准库分支（std::reverse_copy / std::mismatch），语义完全等价。
# 将来升级 Crypto++ 时，只要在新源码的同一位置重打这两处补丁即可。
target_compile_definitions(cryptopp PUBLIC CRYPTOPP_NO_STDEXT_ITERATOR)

target_compile_features(cryptopp PRIVATE cxx_std_17)

if(MSVC)
    # /W0：第三方库静音；/utf-8：源码里有非 ASCII 注释，MSVC 需要它才能正确解析
    target_compile_options(cryptopp PRIVATE /W0 /utf-8)
    target_compile_definitions(cryptopp PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(cryptopp PRIVATE -w)
endif()

# 将来若要被动态库链接，需要 -fPIC；静态库场景下也无害
set_target_properties(cryptopp PROPERTIES POSITION_INDEPENDENT_CODE ON)
