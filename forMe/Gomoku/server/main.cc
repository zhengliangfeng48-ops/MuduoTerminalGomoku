// ============================================================================
// gomoku_server 入口
//
// 启动顺序：
//   1. 解析参数（--help 直接退出）
//   2. 忽略 SIGPIPE —— 否则向已断开的连接写数据会直接杀死进程
//   3. 初始化异步日志（AsyncLogging 内部用 LogFile 做滚动）
//   4. 创建 base EventLoop，构造 ServerApp（内部连接 MySQL）
//   5. app.start() 启动游戏 TcpServer 与压测 HTTP TcpServer
//   6. loop.loop() 进入 Reactor 事件循环
// ============================================================================
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdio>
#include <memory>
#include <string>

#include "muduo/base/AsyncLogging.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"

#include "server/Config.h"
#include "server/ServerApp.h"

namespace {

// muduo 的 Logger::setOutput 接受的是普通函数指针（不是 std::function），
// 因此这里用一个全局指针 + 两个跳板函数把日志转到 AsyncLogging。
muduo::AsyncLogging* g_asyncLog = NULL;

void asyncOutput(const char* msg, int len) {
  if (g_asyncLog != NULL) {
    g_asyncLog->append(msg, len);
  }
}

// 本版本 muduo 的 AsyncLogging 没有 flush()（它按 flushInterval 定时落盘），
// 因此这里的 flush 只需空实现即可，避免注册一个会崩溃的回调。
void asyncFlush() {}

// 创建日志目录（仅一层，够用；已存在返回 true）。
bool ensureDirectory(const std::string& dir) {
  if (dir.empty()) {
    return true;
  }
  if (::mkdir(dir.c_str(), 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

}  // namespace

int main(int argc, char** argv) {
  gomoku::Config config;
  std::string err;
  const gomoku::Config::ParseResult result = config.parse(argc, argv, &err);
  if (result == gomoku::Config::kHelp) {
    gomoku::Config::printUsage(argv[0]);
    return 0;
  }
  if (result == gomoku::Config::kError) {
    std::fprintf(stderr, "参数错误: %s\n\n", err.c_str());
    gomoku::Config::printUsage(argv[0]);
    return 1;
  }

  // 必须忽略 SIGPIPE：客户端断开后继续写会触发 SIGPIPE，默认动作是终止进程。
  ::signal(SIGPIPE, SIG_IGN);

  std::unique_ptr<muduo::AsyncLogging> asyncLog;
  if (!config.logDir.empty()) {
    if (!ensureDirectory(config.logDir)) {
      std::fprintf(stderr, "无法创建日志目录: %s\n", config.logDir.c_str());
      return 1;
    }
    // basename 由 AsyncLogging 内部拼接时间戳与 .log 后缀
    const std::string basename = config.logDir + "/gomoku_server";
    asyncLog.reset(new muduo::AsyncLogging(basename, 200 * 1000 * 1000));
    g_asyncLog = asyncLog.get();
    asyncLog->start();
    muduo::Logger::setOutput(asyncOutput);
    muduo::Logger::setFlush(asyncFlush);
  }
  muduo::Logger::setLogLevel(muduo::Logger::INFO);

  LOG_INFO << "==== gomoku_server starting ====";

  {
    muduo::net::EventLoop loop;
    gomoku::ServerApp app(&loop, config);
    app.start();
    loop.loop();
  }

  LOG_INFO << "==== gomoku_server exiting ====";
  if (asyncLog) {
    asyncLog->stop();
  }
  return 0;
}
