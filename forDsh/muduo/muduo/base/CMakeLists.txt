# muduo_base：基础设施库（日志、线程、时间、文件、队列等）
# 源文件列表与上游 muduo/base/CMakeLists.txt 完全一致。
set(base_SRCS
  AsyncLogging.cc
  Condition.cc
  CountDownLatch.cc
  CurrentThread.cc
  Date.cc
  Exception.cc
  FileUtil.cc
  LogFile.cc
  Logging.cc
  LogStream.cc
  ProcessInfo.cc
  Timestamp.cc
  Thread.cc
  ThreadPool.cc
  TimeZone.cc
  )

add_library(muduo_base STATIC ${base_SRCS})
add_library(muduo::base ALIAS muduo_base)

# PUBLIC：头文件路径与线程依赖需要传递给下游（muduo_net / Gomoku）
target_include_directories(muduo_base PUBLIC ${MUDUO_INCLUDE_DIR})
target_compile_options(muduo_base PRIVATE ${MUDUO_COMPILE_OPTIONS})
target_compile_definitions(muduo_base PUBLIC ${MUDUO_COMPILE_DEFINITIONS})
target_link_libraries(muduo_base PUBLIC Threads::Threads)
if(RT_LIBRARY)
  target_link_libraries(muduo_base PUBLIC ${RT_LIBRARY})
endif()
