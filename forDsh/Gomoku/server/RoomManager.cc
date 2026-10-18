#include "server/RoomManager.h"

#include "common/Protocol.h"
#include "muduo/base/Logging.h"

namespace gomoku {

RoomManager::RoomManager(const RoomCallbacks& callbacks) : callbacks_(callbacks) {}

bool RoomManager::join(const SessionPtr& session, const std::string& roomId, RoomError* err) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!session->roomId().empty()) {
    err->set(kErrAlreadyInRoom, "你已经在房间 " + session->roomId() + " 中，请先 LEAVE");
    return false;
  }

  std::map<std::string, std::shared_ptr<Room> >::iterator it = rooms_.find(roomId);
  if (it == rooms_.end()) {
    std::shared_ptr<Room> room(new Room(roomId, callbacks_));
    it = rooms_.insert(std::make_pair(roomId, room)).first;
    LOG_INFO << "room created: " << roomId;
  }

  if (it->second->userCount() >= kMaxRoomMembers) {
    err->set(kErrRoomFull, "房间人数已满（上限 " + std::to_string(kMaxRoomMembers) + "）");
    return false;
  }

  it->second->onJoin(session);
  return true;
}

void RoomManager::leave(const SessionPtr& session, GameOverInfo* over) {
  std::lock_guard<std::mutex> lock(mutex_);

  const std::string roomId = session->roomId();
  if (roomId.empty()) {
    return;
  }
  std::map<std::string, std::shared_ptr<Room> >::iterator it = rooms_.find(roomId);
  if (it != rooms_.end()) {
    it->second->onLeave(session->nickname(), over);
    if (it->second->empty()) {
      rooms_.erase(it);
      LOG_INFO << "room destroyed: " << roomId;
    }
  }
  session->clearRoomId();
}

void RoomManager::onDisconnect(const SessionPtr& session, GameOverInfo* over) {
  leave(session, over);
}

bool RoomManager::requireRoomLocked(const SessionPtr& session, RoomError* err,
                                    std::shared_ptr<Room>* out) const {
  if (session->roomId().empty()) {
    err->set(kErrNotInRoom, "你还没有进入任何房间，请先 JOIN <房间号>");
    return false;
  }
  std::map<std::string, std::shared_ptr<Room> >::const_iterator it = rooms_.find(session->roomId());
  if (it == rooms_.end()) {
    err->set(kErrNotInRoom, "房间不存在");
    return false;
  }
  *out = it->second;
  return true;
}

bool RoomManager::sit(const SessionPtr& session, RoomError* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  if (!requireRoomLocked(session, err, &room)) {
    return false;
  }
  return room->sit(session, err);
}

bool RoomManager::stand(const SessionPtr& session, RoomError* err, GameOverInfo* over) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  if (!requireRoomLocked(session, err, &room)) {
    return false;
  }
  return room->stand(session, err, over);
}

bool RoomManager::setReady(const SessionPtr& session, RoomError* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  if (!requireRoomLocked(session, err, &room)) {
    return false;
  }
  return room->setReady(session, err);
}

bool RoomManager::playMove(const SessionPtr& session, int x, int y, RoomError* err,
                           GameOverInfo* over) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  if (!requireRoomLocked(session, err, &room)) {
    return false;
  }
  return room->playMove(session, x, y, err, over);
}

bool RoomManager::chat(const SessionPtr& session, const std::string& text, RoomError* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  if (!requireRoomLocked(session, err, &room)) {
    return false;
  }
  return room->chat(session, text, err);
}

void RoomManager::sendBoard(const SessionPtr& session) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  RoomError err;
  if (!requireRoomLocked(session, &err, &room)) {
    session->sendError(err.code.c_str(), err.message);
    return;
  }
  room->sendBoard(session);
}

void RoomManager::sendWho(const SessionPtr& session) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::shared_ptr<Room> room;
  RoomError err;
  if (!requireRoomLocked(session, &err, &room)) {
    session->sendError(err.code.c_str(), err.message);
    return;
  }
  room->sendWho(session);
}

std::string RoomManager::roomListPayload() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string out;
  for (std::map<std::string, std::shared_ptr<Room> >::const_iterator it = rooms_.begin();
       it != rooms_.end(); ++it) {
    if (!out.empty()) {
      out.push_back('|');
    }
    out += it->second->describe();
  }
  return out;
}

std::size_t RoomManager::roomCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rooms_.size();
}

std::size_t RoomManager::playingCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t n = 0;
  for (std::map<std::string, std::shared_ptr<Room> >::const_iterator it = rooms_.begin();
       it != rooms_.end(); ++it) {
    if (it->second->playing()) {
      ++n;
    }
  }
  return n;
}

}  // namespace gomoku
