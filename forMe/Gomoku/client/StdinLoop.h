// ============================================================================
// 把标准输入搬到独立线程
//
// 为什么需要单独线程？
//   * EventLoop 只处理网络事件，阻塞在 getline() 上会让整个客户端卡死；
//   * 所以用一个线程专职阻塞读 stdin，读到整行后通过
//     EventLoop::runInLoop() 投递回 IO 线程执行业务逻辑
//     —— 这是 muduo 中跨线程操作连接的标准做法。
// ============================================================================
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "muduo/net/EventLoop.h"

namespace gomoku {

class StdinLoop {
 public:
  typedef std::function<void(const std::string&)> Handler;

  StdinLoop(muduo::net::EventLoop* loop, Handler handler);
  ~StdinLoop();

  void start();

 private:
  void threadFunc();

  muduo::net::EventLoop* loop_;
  Handler handler_;
  std::thread thread_;
  std::atomic<bool> running_;
};

}  // namespace gomoku
