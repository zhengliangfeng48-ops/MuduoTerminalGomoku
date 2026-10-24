# ============================================================================
# muduo_net：自己实现的 Reactor 网络库（EventLoop / Channel / Poller / TcpConnection …）
#
# 与 forDsh/muduo 的差别同样只有"源文件怎么收集"：这里用 GLOB 收集，
# 写完一个 .cc 就能编译一次（文件清单见 ../../README.md，目标 20 个 .cc）。
#
# 保留 upstream 的特性探测：老内核没有 accept4 时退化为 accept + fcntl
# （对应 SocketsOps.cc 里的 #ifdef NO_ACCEPT4 分支）。
# ============================================================================
include(CheckFunctionExists)
check_function_exists(accept4 HAVE_ACCEPT4)

file(GLOB MUDUO_NET_SRCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/poller/*.cc")

if(NOT MUDUO_NET_SRCS)
  message(STATUS "muduo_net: 目录里还没有 .cc，生成 INTERFACE 占位库（写完后自动变静态库）")
  add_library(muduo_net INTERFACE)
  target_include_directories(muduo_net INTERFACE ${MUDUO_INCLUDE_DIR})
  target_link_libraries(muduo_net INTERFACE muduo_base)
  add_library(muduo::net ALIAS muduo_net)
else()
  list(LENGTH MUDUO_NET_SRCS _ndone)
  message(STATUS "muduo_net: 已收集 ${_ndone} 个 .cc（目标 19 个）")

  # 只有 SocketsOps.cc 已经写了才需要加 -DNO_ACCEPT4
  if(NOT HAVE_ACCEPT4)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SocketsOps.cc")
      set_source_files_properties(SocketsOps.cc PROPERTIES COMPILE_FLAGS "-DNO_ACCEPT4")
    endif()
  endif()

  add_library(muduo_net STATIC ${MUDUO_NET_SRCS})
  add_library(muduo::net ALIAS muduo_net)

  target_include_directories(muduo_net PUBLIC ${MUDUO_INCLUDE_DIR})
  target_compile_options(muduo_net PRIVATE ${MUDUO_COMPILE_OPTIONS})
  target_compile_definitions(muduo_net PUBLIC ${MUDUO_COMPILE_DEFINITIONS})
  # muduo_net 依赖 muduo_base（日志、Thread、Timestamp 等）
  target_link_libraries(muduo_net PUBLIC muduo_base)
endif()
