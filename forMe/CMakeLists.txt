# ============================================================================
# 自研工程（学习用）：muduo 由自己手写，Gomoku 从 forDsh 复制
#   muduo/   —— 按 forDsh/muduo 的结构自己实现的 muduo（base + net，静态库）
#   Gomoku/  —— 基于该库的终端五子棋服务端与客户端（与参考实现同一份代码）
#
# 构建：
#   cmake -S . -B build && cmake --build build -j
# 产物（muduo 写完后）：
#   build/lib/libmuduo_base.a  build/lib/libmuduo_net.a
#   build/bin/gomoku_server    build/bin/gomoku_client    build/bin/gomoku_board_test
#
# 两个有用的选项：
#   -DUSE_REFERENCE_MUDUO=ON
#       改用 forDsh/muduo 这份"参考实现"来编译，用来：
#         (a) 业务代码先行验证（muduo 还没写完时也能跑 e2e / 压测）；
#         (b) A/B 对照：同一份 Gomoku 代码分别链接两份 muduo，比性能。
#   -DMUDUO_REF_DIR=<path>   参考实现路径，默认 /forDsh/muduo（WSL 里的软链）
# ============================================================================
cmake_minimum_required(VERSION 3.12)
project(gomoku_improved CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 产物集中到 build/bin 与 build/lib，与参考实现保持一致（脚本与文档都依赖这个布局）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# -rdynamic 让 Exception/backtrace 能打印函数名（上游 muduo 也是这样用的）
add_compile_options(-rdynamic)

# ---------------------------------------------------------------------------
# 选哪份 muduo
# ---------------------------------------------------------------------------
option(USE_REFERENCE_MUDUO "用 forDsh 里的参考 muduo 编译（业务先行验证 / A-B 对照）" OFF)
set(MUDUO_REF_DIR "/forDsh/muduo" CACHE PATH "参考实现 muduo 目录（其下有 muduo/base、muduo/net）")

if(USE_REFERENCE_MUDUO)
  if(NOT EXISTS "${MUDUO_REF_DIR}/muduo/net/EventLoop.h")
    message(FATAL_ERROR
      "USE_REFERENCE_MUDUO=ON 但找不到参考实现：${MUDUO_REF_DIR}/muduo/net/EventLoop.h\n"
      "请用 -DMUDUO_REF_DIR=<forDsh/muduo 的绝对路径> 指定，例如：\n"
      "  cmake -S . -B build -DUSE_REFERENCE_MUDUO=ON -DMUDUO_REF_DIR=/forDsh/muduo")
  endif()
  message(STATUS "muduo 来源：参考实现 ${MUDUO_REF_DIR}")
  add_subdirectory("${MUDUO_REF_DIR}" "${CMAKE_BINARY_DIR}/refmuduo")
else()
  message(STATUS "muduo 来源：自研实现 （本仓库 muduo/，未写完时 Gomoku 编译失败属预期）")
  add_subdirectory(muduo)
endif()

add_subdirectory(Gomoku)
