// ============================================================================
// 一个客户端连接对应的会话（协议状态机的载体）
//
// 生命周期与线程模型：
//   * ServerApp 在 onConnection 里创建 Session，并用 TcpConnection::setContext()
//     把它挂在连接上 —— 连接持有 Session 的强引用，连接销毁时 Session 随之销毁；
//   * Session 反过来只持有 TcpConnection 的 **weak_ptr**，避免循环引用导致泄漏；
//   * authed_ / nickname_ / userId_ / roomId_ 这四个字段只在“本连接所属的 IO 线程”
//     读写，因此不需要加锁；
//   * 唯一会被其它线程调用的是 send()，它内部先 lock weak_ptr 再调用
//     TcpConnection::send()，而后者本身是线程安全的（内部会 runInLoop 到目标 loop）。
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "muduo/net/Buffer.h"
#include "muduo/net/TcpConnection.h"

namespace gomoku {

class ServerApp;
class Session;  // 前置声明：下面的 typedef 需要它

typedef std::shared_ptr<Session> SessionPtr;
typedef std::weak_ptr<Session> SessionWeakPtr;
typedef std::weak_ptr<muduo::net::TcpConnection> TcpConnectionWeakPtr;

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(const muduo::net::TcpConnectionPtr& conn, ServerApp* app);

  // ------------------------- 发送 -------------------------
  // 发送一整行（自动补 '\n'）。可在任意线程调用。
  void send(const std::string& line);
  void sendError(const char* code, const std::string& message);
  void sendOk(const std::string& message);

  // 主动关闭连接（QUIT 或协议错误时使用）。
  void close();

  // ------------------------- 认证状态 -------------------------
  bool authed() const { return authed_; }
  const std::string& nickname() const { return nickname_; }
  long long userId() const { return userId_; }
  void setAuthed(long long userId, const std::string& nickname);

  // ------------------------- 房间状态 -------------------------
  const std::string& roomId() const { return roomId_; }
  void setRoomId(const std::string& id) { roomId_ = id; }
  void clearRoomId() { roomId_.clear(); }

  // ------------------------- 协议处理 -------------------------
  // 处理一行（不含 '\n'）文本；由 ServerApp::onMessage 调用。
  void handleLine(const std::string& line);

 private:
  void dispatchCommand(const std::string& cmd, const std::string& rest,
                       const std::vector<std::string>& args);

  TcpConnectionWeakPtr conn_;
  ServerApp* app_;
  bool authed_;
  long long userId_;
  std::string nickname_;
  std::string roomId_;
};

}  // namespace gomoku
