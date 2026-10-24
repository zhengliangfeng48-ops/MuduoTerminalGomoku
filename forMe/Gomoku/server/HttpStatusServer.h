// ============================================================================
// 极简 HTTP 状态服务 —— 专供 webbench 压测使用
//
// 为什么单独起一个 HTTP 服务？
//   * 游戏业务用的是自定义文本协议，webbench 只会发 HTTP 请求，无法直接压测；
//   * 把压测端点做成同进程内的**独立 TcpServer + 独立 EventLoopThread**，
//     既能真实压到 muduo 的 accept/读写路径，又不会污染游戏协议；
//   * webbench 默认发 HTTP/1.0 并一直读到对端关闭，因此这里统一
//     “带 Content-Length + Connection: close，写完后 shutdown”。
//
// 端点：
//   GET /              简短 HTML 页面（体积刻意保持很小）
//   GET /health        {"status":"ok"}
//   GET /stats         运行统计 JSON
//   GET /favicon.ico   204
//   其它               404
// ============================================================================
#pragma once

#include <functional>
#include <string>

#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/TcpServer.h"

namespace gomoku {

class HttpStatusServer {
 public:
  // 由 ServerApp 注入：返回 /stats 的 JSON 文本。
  typedef std::function<std::string()> StatsProvider;

  HttpStatusServer(muduo::net::EventLoop* loop, const muduo::net::InetAddress& listenAddr,
                   const std::string& name, StatsProvider stats);

  void start();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);
  void onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf,
                 muduo::Timestamp receiveTime);

  muduo::net::TcpServer server_;
  StatsProvider stats_;
};

}  // namespace gomoku
