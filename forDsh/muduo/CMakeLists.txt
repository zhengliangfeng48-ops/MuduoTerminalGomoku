# ============================================================================
# muduo 核心库构建脚本
#
# 说明：
#   本文件是本项目唯一新增于上游源码之外的构建描述，muduo 的 .h/.cc 全部
#   与 chenshuo/muduo master 保持一致，未做任何逻辑修改。
#   上游根 CMakeLists.txt 会强制查找 Boost / Protobuf / CURL / ZLIB / c-ares /
#   libmicrohttpd 并编译 examples、tests、http、inspect、protobuf 等模块，
#   本项目只需要 base 与 net 两个静态库，因此改写为下面这份精简版本。
#
# 与上游构建参数的差异（仅此三处）：
#   * 去掉 -Werror            ：gcc 11.3 下会因上游遗留告警而中断编译
#   * 去掉 -Wconversion       ：同上
#   * 去掉 -Wold-style-cast   ：同上（上游在 C++11 模式下也未完全清理）
#   其余 -g -Wall -Wextra -Wno-unused-parameter -Wpointer-arith -Wshadow
#   -Wwrite-strings -DCHECK_PTHREAD_RETURN_VALUE -D_FILE_OFFSET_BITS=64 均保留。
# ============================================================================
cmake_minimum_required(VERSION 3.10)
project(muduo CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 头文件以 #include "muduo/base/Logging.h" 的方式被引用，
# 所以对外暴露的 include 根目录就是本目录（即 .../forDsh/muduo）。
set(MUDUO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
# 上游 base 依赖 librt；glibc 2.34 之后 rt 已并入 libc，
# 这里做存在性判断，避免在只提供 stub 的发行版上链接失败。
find_library(RT_LIBRARY rt)

# 组件级编译选项。用 target_compile_options 而不是全局 CMAKE_CXX_FLAGS，
# 以免污染 Gomoku 子工程的编译参数。
set(MUDUO_COMPILE_OPTIONS
  -g
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wpointer-arith
  -Wshadow
  -Wwrite-strings
)
set(MUDUO_COMPILE_DEFINITIONS
  CHECK_PTHREAD_RETURN_VALUE
  _FILE_OFFSET_BITS=64
)

add_subdirectory(muduo/base)
add_subdirectory(muduo/net)
