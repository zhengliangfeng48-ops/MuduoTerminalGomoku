// ============================================================================
// 服务端命令行 + 环境变量配置
//
// 优先级：命令行参数 > 环境变量(GOMOKU_*) > 内置默认值
// 之所以不引入 boost::program_options，是为了让依赖保持最小（只需要 boost 头文件）。
// ============================================================================
#pragma once

#include <string>

#include "server/Db.h"

namespace gomoku {

struct Config {
  // 监听
  std::string listenAddr;   // 默认 0.0.0.0
  unsigned short port;      // 游戏协议端口，默认 9527
  unsigned short httpPort;  // webbench 压测用 HTTP 端口，默认 8080
  int ioThreads;            // TcpServer 的 IO 线程数，0 表示全部在 base loop
  // 日志
  std::string logDir;       // 空字符串表示只输出到 stdout
  // 数据库
  DbParams db;

  Config();

  enum ParseResult {
    kOk = 0,
    kHelp,   // 打印用法后退出
    kError,  // 参数错误
  };

  // 解析命令行；失败时把错误信息写入 err。
  ParseResult parse(int argc, char** argv, std::string* err);

  static void printUsage(const char* prog);
};

}  // namespace gomoku
