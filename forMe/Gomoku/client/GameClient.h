// ============================================================================
// 客户端主体：muduo::net::TcpClient 封装 + 协议消息分发 + 本地命令解析
//
// 线程模型：
//   * 主线程运行 EventLoop，TcpClient 的所有回调都在这个线程；
//   * 另有一个 stdin 线程（见 StdinLoop）只负责 getline，然后通过
//     EventLoop::runInLoop 把整行投递回 IO 线程；
//   * 因此 conn_ / ui_ / authed_ 这些状态只被 IO 线程读写，无需加锁。
// ============================================================================
#pragma once

#include <string>

#include "muduo/net/Buffer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpClient.h"
#include "muduo/net/TcpConnection.h"

#include "client/TerminalUI.h"

namespace gomoku {

class GameClient {
 public:
  struct Options {
    std::string host;
    unsigned short port;
    std::string nickname;
    std::string password;
    bool doRegister;  // true=REGISTER，false=LOGIN
    bool color;

    Options();
  };

  GameClient(muduo::net::EventLoop* loop, const Options& options);

  // 发起连接（异步）。
  void connect();
  // 处理一行用户输入；由 stdin 线程经 runInLoop 投递到 IO 线程后调用。
  void handleInput(const std::string& line);

  TerminalUI* ui() { return &ui_; }

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);
  void onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf,
                 muduo::Timestamp receiveTime);
  void onLine(const std::string& line);
  void sendLine(const std::string& line);
  void handleLocalCommand(const std::string& cmd, const std::string& rest);

  // 解析落子输入：支持 "H8"（列字母+行号）与 "8 8" / "8,8"（列 行）。
  static bool parseMoveInput(const std::string& text, int* x, int* y);

  muduo::net::EventLoop* loop_;
  Options options_;
  muduo::net::TcpClient client_;
  muduo::net::TcpConnectionPtr conn_;
  TerminalUI ui_;
  bool authed_;
};

}  // namespace gomoku
