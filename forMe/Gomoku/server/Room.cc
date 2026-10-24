#include "server/Room.h"

#include "common/Protocol.h"
#include "muduo/base/Logging.h"

namespace gomoku {

Room::Room(const std::string& id, const RoomCallbacks& callbacks)
    : id_(id), callbacks_(callbacks), blackReady_(false), whiteReady_(false) {}

Room::State Room::state() const {
  if (game_.running()) {
    return kPlaying;
  }
  if (!black_.empty() && !white_.empty()) {
    return kWaitReady;
  }
  return kIdle;
}

const char* Room::stateText() const {
  switch (state()) {
    case kPlaying:
      return kStatePlaying;
    case kWaitReady:
      return kStateWaitReady;
    default:
      return kStateIdle;
  }
}

bool Room::hasMember(const std::string& nick) const {
  for (std::size_t i = 0; i < users_.size(); ++i) {
    if (users_[i].nick == nick) {
      return true;
    }
  }
  return false;
}

bool Room::isSeat(const std::string& nick) const {
  return (!black_.empty() && black_ == nick) || (!white_.empty() && white_ == nick);
}

std::string Room::displayOrDash(const std::string& s) {
  return s.empty() ? std::string("-") : s;
}

// ------------------------------ 广播 ------------------------------
void Room::broadcast(const std::string& line) const {
  for (std::size_t i = 0; i < users_.size(); ++i) {
    SessionPtr session = users_[i].session.lock();
    if (session) {
      session->send(line);
    }
  }
}

void Room::broadcastExcept(const std::string& nick, const std::string& line) const {
  for (std::size_t i = 0; i < users_.size(); ++i) {
    if (users_[i].nick == nick) {
      continue;
    }
    SessionPtr session = users_[i].session.lock();
    if (session) {
      session->send(line);
    }
  }
}

void Room::broadcastBoard() const {
  // BOARD <225 个棋子字符> <最后落子 x> <最后落子 y>
  broadcast(std::string(kEvBoard) + " " + game_.boardString() + " " + std::to_string(game_.lastX()) +
            " " + std::to_string(game_.lastY()));
}

void Room::sendBoard(const SessionPtr& session) const {
  session->send(std::string(kEvBoard) + " " + game_.boardString() + " " +
                std::to_string(game_.lastX()) + " " + std::to_string(game_.lastY()));
}

void Room::sendSeats() const {
  broadcast(std::string(kEvSeats) + " " + displayOrDash(black_) + " " + displayOrDash(white_) +
            " " + stateText());
}

void Room::sendUsers() const {
  std::string joined;
  for (std::size_t i = 0; i < users_.size(); ++i) {
    if (!joined.empty()) {
      joined.push_back(',');
    }
    joined += users_[i].nick;
  }
  broadcast(std::string(kEvUsers) + " " + (joined.empty() ? std::string("-") : joined));
}

// ------------------------------ 进出房间 ------------------------------
void Room::onJoin(const SessionPtr& session) {
  Member m;
  m.nick = session->nickname();
  m.session = session;
  users_.push_back(m);
  session->setRoomId(id_);

  // 先通知其他成员，再给新成员推完整快照（顺序对客户端更友好）。
  broadcastExcept(m.nick, std::string(kEvJoined) + " " + m.nick);
  sendSnapshot(session);
  LOG_INFO << "room " << id_ << ": " << m.nick << " joined (" << users_.size() << " users)";
}

void Room::onLeave(const std::string& nick, GameOverInfo* over) {
  if (!hasMember(nick)) {
    return;
  }

  // 对局中离开 -> 对手直接胜
  if (game_.running() && isSeat(nick)) {
    const bool wasBlack = (nick == black_);
    finishGame(wasBlack ? GomokuGame::kWhiteWin : GomokuGame::kBlackWin, kReasonForfeitLeft, over);
  }

  if (black_ == nick) {
    black_.clear();
    blackReady_ = false;
  }
  if (white_ == nick) {
    white_.clear();
    whiteReady_ = false;
  }

  for (std::vector<Member>::iterator it = users_.begin(); it != users_.end(); ++it) {
    if (it->nick == nick) {
      users_.erase(it);
      break;
    }
  }

  broadcast(std::string(kEvLeft) + " " + nick);
  sendUsers();
  sendSeats();
  LOG_INFO << "room " << id_ << ": " << nick << " left (" << users_.size() << " users)";
}

// ------------------------------ 座位 ------------------------------
bool Room::sit(const SessionPtr& session, RoomError* err) {
  const std::string& nick = session->nickname();

  if (game_.running()) {
    err->set(kErrGameRunning, "房间里正在对局，请等待本局结束");
    return false;
  }
  if (isSeat(nick)) {
    err->set(kErrBadCmd, "你已经坐在座位上了");
    return false;
  }
  if (!black_.empty() && !white_.empty()) {
    err->set(kErrSeatFull, "两个座位都有人了，只能观战");
    return false;
  }

  // 黑位优先；两座坐满即自动开局（对应“无人对弈即可上座”）。
  if (black_.empty()) {
    black_ = nick;
  } else {
    white_ = nick;
  }
  blackReady_ = false;
  whiteReady_ = false;

  broadcast(std::string(kEvSys) + " " + nick + " 坐上了" + (black_ == nick ? "黑方" : "白方") + "座位");
  sendSeats();

  if (!black_.empty() && !white_.empty()) {
    startGame();
  }
  return true;
}

bool Room::stand(const SessionPtr& session, RoomError* err, GameOverInfo* over) {
  const std::string& nick = session->nickname();
  if (!isSeat(nick)) {
    err->set(kErrNotSeated, "你没有坐下");
    return false;
  }

  // 对局中起身视为认输：先结算，再腾出座位。
  if (game_.running()) {
    const bool wasBlack = (nick == black_);
    finishGame(wasBlack ? GomokuGame::kWhiteWin : GomokuGame::kBlackWin, kReasonForfeitStand, over);
  }

  if (black_ == nick) {
    black_.clear();
  }
  if (white_ == nick) {
    white_.clear();
  }
  blackReady_ = false;
  whiteReady_ = false;

  broadcast(std::string(kEvSys) + " " + nick + " 起身离开了座位");
  sendSeats();
  return true;
}

bool Room::setReady(const SessionPtr& session, RoomError* err) {
  const std::string& nick = session->nickname();
  if (game_.running()) {
    err->set(kErrGameRunning, "对局进行中无需准备");
    return false;
  }
  if (!isSeat(nick)) {
    err->set(kErrNotSeated, "请先坐下再准备");
    return false;
  }

  if (nick == black_) {
    blackReady_ = true;
  } else {
    whiteReady_ = true;
  }
  broadcast(std::string(kEvSys) + " " + nick + " 已准备");

  // 双方都准备好 -> 开新局
  if (blackReady_ && whiteReady_ && !black_.empty() && !white_.empty()) {
    startGame();
  }
  return true;
}

// ------------------------------ 对局 ------------------------------
void Room::startGame() {
  game_.reset();
  blackReady_ = false;
  whiteReady_ = false;

  sendSeats();  // 状态变为 PLAYING
  broadcast(std::string(kEvStart) + " " + black_ + " " + white_);
  broadcastBoard();
  broadcast(std::string(kEvTurn) + " " + black_);
  LOG_INFO << "room " << id_ << ": game start " << black_ << " vs " << white_;
}

void Room::finishGame(GomokuGame::Result result, const std::string& reason, GameOverInfo* over) {
  game_.finish(result);

  std::string winner;
  const char* kind = "WON";
  if (result == GomokuGame::kBlackWin) {
    winner = black_;
  } else if (result == GomokuGame::kWhiteWin) {
    winner = white_;
  } else {
    winner = "-";
    kind = "DRAW";
  }

  // 结束时的棋盘再发一次，客户端可据此展示终局。
  // 只发 OVER 事件：客户端据此渲染"对局结束/胜者/原因"，
  // 不再额外发一条 SYS，避免终端里同一件事播报两次。
  broadcastBoard();
  broadcast(std::string(kEvOver) + " " + kind + " " + winner + " " + reason);

  if (over != NULL) {
    over->valid = true;
    over->roomId = id_;
    over->black = black_;
    over->white = white_;
    over->result = result;
    over->reason = reason;
    over->moves = game_.moves();
  }

  blackReady_ = false;
  whiteReady_ = false;
  sendSeats();  // 状态回到 WAITREADY / IDLE
}

bool Room::playMove(const SessionPtr& session, int x, int y, RoomError* err, GameOverInfo* over) {
  const std::string& nick = session->nickname();

  if (!game_.running()) {
    err->set(kErrBadCmd, "当前没有进行中的对局");
    return false;
  }
  if (!isSeat(nick)) {
    err->set(kErrNotSeated, "观战者不能落子");
    return false;
  }

  const bool wasBlack = (game_.current() == GomokuGame::kBlack);
  const std::string& currentNick = wasBlack ? black_ : white_;
  if (nick != currentNick) {
    err->set(kErrNotYourTurn, "还没轮到你落子，当前轮到: " + currentNick);
    return false;
  }
  if (!game_.place(x, y)) {
    err->set(kErrIllegalMove, "非法落子：坐标越界或该点已有棋子");
    return false;
  }

  broadcast(std::string(kEvPlaced) + " " + std::to_string(x) + " " + std::to_string(y) + " " + nick +
            " " + (wasBlack ? "B" : "W"));

  if (game_.running()) {
    broadcastBoard();
    const std::string& nextNick = wasBlack ? white_ : black_;
    broadcast(std::string(kEvTurn) + " " + nextNick);
  } else {
    // 五连或满盘：reason 由棋盘自身给出
    const std::string reason = (game_.result() == GomokuGame::kDraw) ? kReasonFull : kReasonFive;
    finishGame(game_.result(), reason, over);
  }
  return true;
}

bool Room::chat(const SessionPtr& session, const std::string& text, RoomError* err) {
  const std::string& nick = session->nickname();

  // 需求：对弈时间只有对弈双方可以发消息。
  if (game_.running() && !isSeat(nick)) {
    err->set(kErrChatLocked, "对局进行中，只有对弈双方可以聊天");
    return false;
  }
  if (text.empty()) {
    err->set(kErrBadArg, "聊天内容不能为空");
    return false;
  }

  broadcast(std::string(kEvChat) + " " + nick + " " + text);
  return true;
}

// ------------------------------ 查询 ------------------------------
void Room::sendSnapshot(const SessionPtr& session) const {
  session->send(std::string(kEvRoom) + " " + id_ + " " + stateText() + " " + displayOrDash(black_) +
                " " + displayOrDash(white_) + " " + session->nickname());
  sendWho(session);
  session->send(std::string(kEvSeats) + " " + displayOrDash(black_) + " " + displayOrDash(white_) +
                " " + stateText());
  if (game_.running()) {
    sendBoard(session);
    const std::string& currentNick = (game_.current() == GomokuGame::kBlack) ? black_ : white_;
    session->send(std::string(kEvTurn) + " " + currentNick);
  }
}

void Room::sendWho(const SessionPtr& session) const {
  std::string joined;
  for (std::size_t i = 0; i < users_.size(); ++i) {
    if (!joined.empty()) {
      joined.push_back(',');
    }
    joined += users_[i].nick;
  }
  session->send(std::string(kEvUsers) + " " + (joined.empty() ? std::string("-") : joined));
}

std::string Room::describe() const {
  return id_ + ":" + stateText() + ":" + displayOrDash(black_) + ":" + displayOrDash(white_) + ":" +
         std::to_string(users_.size());
}

}  // namespace gomoku
