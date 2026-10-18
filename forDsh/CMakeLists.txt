# ============================================================================
# forDsh 顶层工程
#   muduo/   —— muduo 核心库（base + net，静态库）
#   Gomoku/  —— 基于该库的终端五子棋服务端与客户端
#
# 构建：
#   cmake -S . -B build && cmake --build build -j
# 产物：
#   build/lib/libmuduo_base.a  build/lib/libmuduo_net.a
#   build/bin/gomoku_server    build/bin/gomoku_client    build/bin/gomoku_board_test
# ============================================================================
cmake_minimum_required(VERSION 3.10)
project(forDsh CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 产物集中到 build/bin 与 build/lib，方便 README 描述与脚本调用
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# -rdynamic 让 Exception/backtrace 能打印函数名（上游 muduo 也是这样用的）
add_compile_options(-rdynamic)

add_subdirectory(muduo)
add_subdirectory(Gomoku)
