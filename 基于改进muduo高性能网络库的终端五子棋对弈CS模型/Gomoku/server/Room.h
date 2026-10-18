// ============================================================================
// 一个房间：成员、座位、对局生命周期与消息广播
//
// 规则（对应需求）：
//   * 输入房间号即可进入；房间不存在则由服务端创建；
//   * 房间内没有正在进行的对局时，坐下即占座；两座坐满立即开局；
//   * 非对弈时间：房间内所有成员（含观战者）都可以聊天；
//     对弈时间：只有黑白两方可以聊天，其他人发 CHAT 会被拒绝；
//   * 对局中起身 / 断线 = 认输。
//
// 线程模型：
//   Room 的所有方法都**假定调用方已持有 RoomManager 的互斥锁**（见 RoomManager）。
//   这样房间内部状态无需再加锁；唯一的“跨线程”动作是 broadcast()，
//   它最终调用 Session::send() -> TcpConnection::send()，这一步本身是线程安全的。
// ============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "server/GomokuGame.h"
#include "server/Session.h"

namespace gomoku {

// 一局结束时的快照，交给 ServerApp 去写数据库（避免在房间锁内做阻塞 IO）
struct GameOverInfo {
  bool valid;
  std::string roomId;
  std::string black;
  std::string white;
  GomokuGame::Result result;
  std::string reason;
  int moves;

  GameOverInfo() : valid(false), result(GomokuGame::kNone), moves(0) {}
};

struct RoomCallbacks {
  std::function<void(const GameOverInfo&)> onGameOver;
};

// 房间操作的错误输出（协议错误码 + 可读信息）
struct RoomError {
  std::string code;
  std::string message;

  void set(const char* c, const std::string& m) {
    code = c;
    message = m;
  }
};

class Room {
 public:
  // 房间状态，与协议里的 <state> 对应
  enum State {
    kIdle = 0,    // 空座或只有一人，可上座
    kWaitReady,   // 两座已满但上一局已结束，等待双方 READY
    kPlaying,     // 对局进行中
  };

  Room(const std::string& id, const RoomCallbacks& callbacks);

  const std::string& id() const { return id_; }
  bool empty() const { return users_.empty(); }
  std::size_t userCount() const { return users_.size(); }
  State state() const;
  bool playing() const { return game_.running(); }
  bool hasMember(const std::string& nick) const;

  // --------------------------- 成员进出 ---------------------------
  // 新成员进入：登记成员 + 推送房间快照。
  void onJoin(const SessionPtr& session);
  // 成员离开（LEAVE / 断线）：必要时判负，然后移出成员与座位。
  void onLeave(const std::string& nick, GameOverInfo* over);

  // --------------------------- 座位与对局 ---------------------------
  bool sit(const SessionPtr& session, RoomError* err);
  bool stand(const SessionPtr& session, RoomError* err, GameOverInfo* over);
  bool setReady(const SessionPtr& session, RoomError* err);
  bool playMove(const SessionPtr& session, int x, int y, RoomError* err, GameOverInfo* over);
  bool chat(const SessionPtr& session, const std::string& text, RoomError* err);

  // --------------------------- 查询 ---------------------------
  void sendSnapshot(const SessionPtr& session) const;
  void sendBoard(const SessionPtr& session) const;
  void sendWho(const SessionPtr& session) const;
  // 形如 "1001:PLAYING:alice:bob:2"，供 ROOMS 命令使用
  std::string describe() const;

 private:
  void broadcast(const std::string& line) const;
  void broadcastExcept(const std::string& nick, const std::string& line) const;
  void broadcastBoard() const;
  void sendSeats() const;
  void sendUsers() const;

  void startGame();
  void finishGame(GomokuGame::Result result, const std::string& reason, GameOverInfo* over);

  bool isSeat(const std::string& nick) const;
  const char* stateText() const;
  static std::string displayOrDash(const std::string& s);

  struct Member {
    std::string nick;
    SessionWeakPtr session;
  };

  std::string id_;
  RoomCallbacks callbacks_;
  std::vector<Member> users_;

  // 座位：空串表示无人。黑方先手。
  std::string black_;
  std::string white_;
  bool blackReady_;
  bool whiteReady_;

  GomokuGame game_;
};

}  // namespace gomoku
