// ============================================================================
// 服务端主体：装配 TcpServer、分发协议命令、维护在线表、写库、统计
//
// 线程模型（重点）：
//   * main 线程运行 base EventLoop；
//   * TcpServer 通过 setThreadNum(N) 起 N 个 IO 线程，新连接按轮转分配；
//     每条连接的 Session 状态只在该连接所属 IO 线程访问，不需要锁；
//   * 跨线程共享的数据只有三处，各自有明确的保护：
//       1) RoomManager / Room   —— 一把 std::mutex（见 RoomManager.h）
//       2) AuthService / Db     —— 各自一把 std::mutex
//       3) online_ 在线表与统计 —— 一把 std::mutex + std::atomic
//   * 房间广播最终调用 Session::send() -> TcpConnection::send()，线程安全。
// ============================================================================
#pragma once

#include <atomic>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/net/TcpServer.h"

#include "server/AuthService.h"
#include "server/Config.h"
#include "server/Db.h"
#include "server/HttpStatusServer.h"
#include "server/RoomManager.h"
#include "server/Session.h"

namespace gomoku {

class ServerApp {
 public:
  ServerApp(muduo::net::EventLoop* loop, const Config& config);
  ~ServerApp();

  // 启动游戏端口与压测 HTTP 端口。
  void start();

  // ------------------------- Session 回调 -------------------------
  void handleRegister(Session* session, const std::vector<std::string>& args);
  void handleLogin(Session* session, const std::vector<std::string>& args);
  void handleJoin(Session* session, const std::vector<std::string>& args);
  void handleLeave(Session* session);
  void handleSit(Session* session);
  void handleStand(Session* session);
  void handleReady(Session* session);
  void handleMove(Session* session, const std::vector<std::string>& args);
  void handleChat(Session* session, const std::string& text);
  void handleBoard(Session* session);
  void handleWho(Session* session);
  void handleRooms(Session* session);
  void handleStats(Session* session);

  // /stats 端点使用的运行统计（JSON）
  std::string statsJson();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);
  void onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf,
                 muduo::Timestamp receiveTime);
  void onDisconnect(const SessionPtr& session);

  // 一局结束：先落库，再把最新战绩推给双方。
  void onGameOver(const GameOverInfo& info);
  void sendInfo(const std::string& nickname);

  // 在线表：昵称 -> 会话。用于“同一昵称只允许一个连接”与跨线程推送战绩。
  bool addOnline(const std::string& nickname, const SessionPtr& session);
  void removeOnline(const std::string& nickname);
  SessionPtr findOnline(const std::string& nickname) const;

  SessionPtr sessionOf(const muduo::net::TcpConnectionPtr& conn) const;

  muduo::net::EventLoop* loop_;
  Config config_;

  std::unique_ptr<muduo::net::TcpServer> server_;
  std::unique_ptr<muduo::net::EventLoopThread> httpThread_;
  std::unique_ptr<HttpStatusServer> httpServer_;

  Db db_;
  AuthService auth_;
  RoomManager rooms_;

  mutable std::mutex onlineMutex_;
  std::map<std::string, SessionWeakPtr> online_;

  std::atomic<unsigned long long> totalConnections_;
  std::atomic<unsigned long long> totalGames_;
  std::atomic<unsigned long long> totalChats_;
  std::atomic<unsigned long long> peakOnline_;

  std::time_t startedAt_;
};

}  // namespace gomoku
