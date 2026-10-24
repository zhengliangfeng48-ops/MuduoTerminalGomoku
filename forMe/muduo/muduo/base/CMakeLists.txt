# ============================================================================
# muduo_base：自己实现的基础设施库（日志 / 线程 / 时间 / 文件 / 队列）
#
# 与 forDsh/muduo 的唯一差别是"源文件怎么收集"：
#   forDsh 版：显式列出 15 个 .cc（照上游写法）
#   本工程  ：GLOB 收集目录里已存在的 .cc
#
# 原因：这份 muduo 是一个字一个字写出来的。GLOB 让你"写完一个文件就能编译一次"，
#       不必每加一个文件就回来改 CMakeLists。
#       文件清单以 ../../README.md 为准（15 个 .cc + 27 个头文件），
#       缺哪些用 ./scripts/progress.sh 查。
#
# 空目录守卫：一个 .cc 都还没有时，生成 INTERFACE 占位库，
#       这样 configure 仍然成功（Gomoku 会因为缺头文件编译失败，属预期）。
#       写出第一个 .cc 后，自动变成真实静态库，无需改本文件。
# ============================================================================
file(GLOB MUDUO_BASE_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

if(NOT MUDUO_BASE_SRCS)
  message(STATUS "muduo_base: 目录里还没有 .cc，生成 INTERFACE 占位库（写完后自动变静态库）")
  add_library(muduo_base INTERFACE)
  target_include_directories(muduo_base INTERFACE ${MUDUO_INCLUDE_DIR})
  add_library(muduo::base ALIAS muduo_base)
else()
  list(LENGTH MUDUO_BASE_SRCS _ndone)
  message(STATUS "muduo_base: 已收集 ${_ndone} 个 .cc（目标 15 个）")

  add_library(muduo_base STATIC ${MUDUO_BASE_SRCS})
  add_library(muduo::base ALIAS muduo_base)

  # PUBLIC：头文件路径与线程依赖要传递给下游（muduo_net / Gomoku）
  target_include_directories(muduo_base PUBLIC ${MUDUO_INCLUDE_DIR})
  target_compile_options(muduo_base PRIVATE ${MUDUO_COMPILE_OPTIONS})
  target_compile_definitions(muduo_base PUBLIC ${MUDUO_COMPILE_DEFINITIONS})
  target_link_libraries(muduo_base PUBLIC Threads::Threads)
  if(RT_LIBRARY)
    target_link_libraries(muduo_base PUBLIC ${RT_LIBRARY})
  endif()
endif()
