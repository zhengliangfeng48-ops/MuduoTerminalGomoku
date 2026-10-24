// ============================================================================
// 房间表：负责房间的创建/回收与全局互斥
//
// 为什么这里需要一把全局锁？
//   * TcpServer 开了多个 IO 线程，不同连接可能落在不同线程上，但它们可能
//     操作同一个房间（下棋、观战、聊天），因此房间表与房间内部状态必须是
//     互斥访问的；
//   * 锁的粒度是“整个房间表”，实现简单且不易出错。房间操作都是纯内存计算，
//     持锁时间极短；唯一例外是广播时的 TcpConnection::send()，它在需要时
//     只做内存拷贝 + 唤醒目标 loop，不会阻塞在网络上。
//
// 注意：数据库写入**不在这里做**。一局结束时 Room 只把 GameOverInfo 交回
//      ServerApp，由它在释放房间锁之后再访问数据库，避免慢 IO 拖住其他房间。
// ============================================================================
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "server/Room.h"
#include "server/Session.h"

namespace gomoku {

class RoomManager {
 public:
  explicit RoomManager(const RoomCallbacks& callbacks);

  // 进入房间（不存在则创建）。
  bool join(const SessionPtr& session, const std::string& roomId, RoomError* err);
  // 主动离开房间；对局中离开会判负（通过 over 返回给上层写库）。
  void leave(const SessionPtr& session, GameOverInfo* over);
  // 断线走这里，语义同 leave。
  void onDisconnect(const SessionPtr& session, GameOverInfo* over);

  bool sit(const SessionPtr& session, RoomError* err);
  bool stand(const SessionPtr& session, RoomError* err, GameOverInfo* over);
  bool setReady(const SessionPtr& session, RoomError* err);
  bool playMove(const SessionPtr& session, int x, int y, RoomError* err, GameOverInfo* over);
  bool chat(const SessionPtr& session, const std::string& text, RoomError* err);

  void sendBoard(const SessionPtr& session);
  void sendWho(const SessionPtr& session);

  // 形如 "1001:PLAYING:alice:bob:2|1002:IDLE:-:-:1"
  std::string roomListPayload() const;
  std::size_t roomCount() const;
  std::size_t playingCount() const;

 private:
  // 取出会话所在的房间；失败时填 err。
  bool requireRoomLocked(const SessionPtr& session, RoomError* err,
                         std::shared_ptr<Room>* out) const;

  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<Room> > rooms_;
  RoomCallbacks callbacks_;
};

}  // namespace gomoku
