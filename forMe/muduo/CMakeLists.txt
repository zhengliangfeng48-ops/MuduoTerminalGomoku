# ============================================================================
# muduo 核心库构建脚本
#
# 说明：
#   这份 muduo 是照着 forDsh/muduo（上游 chenshuo/muduo master 的逐字副本）
#   自己重新实现的，用于学习 Reactor 网络库。构建骨架沿用参考实现：
#   上游根 CMakeLists.txt 会强制查找 Boost / Protobuf / CURL / ZLIB / c-ares /
#   libmicrohttpd 并编译 examples、tests、http、inspect、protobuf 等模块，
#   本工程只需要 base 与 net 两个静态库。
#
#   子目录的源文件用 GLOB 收集（参考实现是显式列表），这样写完一个 .cc
#   就能编译一次；文件清单见 ./README.md，进度用 ./scripts/progress.sh 查。
#
# 与参考实现构建参数的差异（仅此三处，与上游的差异相同）：
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
# 所以对外暴露的 include 根目录就是本目录（即本仓库的 muduo/）。
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
