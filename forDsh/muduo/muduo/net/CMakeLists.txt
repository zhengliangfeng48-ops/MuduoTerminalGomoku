# muduo_net：Reactor 网络库（EventLoop / Channel / Poller / TcpConnection ...）
# 源文件列表与上游 muduo/net/CMakeLists.txt 一致：net 核心 + poller 子目录。

# 上游特性探测：老内核没有 accept4，需要退化为 accept + fcntl。
# 该逻辑原样保留（见 SocketsOps.cc 中的 #ifdef NO_ACCEPT4 分支）。
include(CheckFunctionExists)
check_function_exists(accept4 HAVE_ACCEPT4)
if(NOT HAVE_ACCEPT4)
  set_source_files_properties(SocketsOps.cc PROPERTIES COMPILE_FLAGS "-DNO_ACCEPT4")
endif()

set(net_SRCS
  Acceptor.cc
  Buffer.cc
  Channel.cc
  Connector.cc
  EventLoop.cc
  EventLoopThread.cc
  EventLoopThreadPool.cc
  InetAddress.cc
  Poller.cc
  poller/DefaultPoller.cc
  poller/EPollPoller.cc
  poller/PollPoller.cc
  Socket.cc
  SocketsOps.cc
  TcpClient.cc
  TcpConnection.cc
  TcpServer.cc
  Timer.cc
  TimerQueue.cc
  )

add_library(muduo_net STATIC ${net_SRCS})
add_library(muduo::net ALIAS muduo_net)

target_include_directories(muduo_net PUBLIC ${MUDUO_INCLUDE_DIR})
target_compile_options(muduo_net PRIVATE ${MUDUO_COMPILE_OPTIONS})
target_compile_definitions(muduo_net PUBLIC ${MUDUO_COMPILE_DEFINITIONS})
# muduo_net 依赖 muduo_base（日志、Thread、Timestamp 等）
target_link_libraries(muduo_net PUBLIC muduo_base)
