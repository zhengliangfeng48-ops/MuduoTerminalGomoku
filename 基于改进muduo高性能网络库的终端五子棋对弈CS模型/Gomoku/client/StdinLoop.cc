#include "client/StdinLoop.h"

#include <iostream>

namespace gomoku {

StdinLoop::StdinLoop(muduo::net::EventLoop* loop, Handler handler)
    : loop_(loop), handler_(handler), running_(true) {}

StdinLoop::~StdinLoop() {
  // 线程可能正阻塞在 getline() 上，join 会永久卡住，
  // 因此这里只置标志位并 detach（见 start()）。
  running_ = false;
}

void StdinLoop::start() {
  thread_ = std::thread(std::bind(&StdinLoop::threadFunc, this));
  // detach 的原因同上：进程退出时不必等待阻塞在 stdin 上的线程。
  thread_.detach();
}

void StdinLoop::threadFunc() {
  std::string line;
  while (running_) {
    if (!std::getline(std::cin, line)) {
      // stdin 关闭（Ctrl-D）：当作 /quit 处理，让客户端正常退出。
      if (running_) {
        loop_->runInLoop([this]() { handler_("/quit"); });
      }
      return;
    }
    if (!running_) {
      return;
    }
    // 拷贝一份 line 投递到 IO 线程；lambda 捕获 by value，生命周期安全。
    const std::string payload = line;
    Handler h = handler_;
    loop_->runInLoop([h, payload]() { h(payload); });
  }
}

}  // namespace gomoku
