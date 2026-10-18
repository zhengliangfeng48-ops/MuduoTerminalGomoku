#include "server/ServerApp.h"

#include <cstdio>
#include <utility>

#include <boost/any.hpp>

#include "common/Protocol.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Logging.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

namespace gomoku {

ServerApp::ServerApp(muduo::net::EventLoop* loop, const Config& config)
    : loop_(loop),
      config_(config),
      auth_(&db_),
      rooms_(RoomCallbacks{std::bind(&ServerApp::onGameOver, this, std::placeholders::_1)}),
      totalConnections_(0),
      totalGames_(0),
      totalChats_(0),
      peakOnline_(0),
      startedAt_(std::time(NULL)) {
  // 连接数据库。失败不阻止服务启动：对局本身不依赖 DB，只是注册/登录/战绩不可用。
  std::string err;
  if (!db_.connect(config_.db, &err)) {
    LOG_ERROR << "MySQL 连接失败: " << err << "（注册/登录/战绩将不可用，对局仍可进行）";
  }
}

ServerApp::~ServerApp() {
  // muduo 要求 TcpServer 在其所属 EventLoop 线程中析构，
  // 因此 HTTP 服务必须“投递回它自己的 loop”再销毁，并用 CountDownLatch 等待完成。
  if (httpServer_ && httpThread_) {
    muduo::net::EventLoop* httpLoop = httpThread_->startLoop();
    muduo::CountDownLatch latch(1);
    httpLoop->runInLoop([this, &latch]() {
      httpServer_.reset();
      latch.countDown();
    });
    latch.wait();
    httpLoop->quit();
  }
}

void ServerApp::start() {
  server_.reset(new muduo::net::TcpServer(
      loop_, muduo::net::InetAddress(config_.listenAddr, config_.port), "gomoku-server"));
  server_->setThreadNum(config_.ioThreads);
  server_->setConnectionCallback(
      std::bind(&ServerApp::onConnection, this, std::placeholders::_1));
  server_->setMessageCallback(
      std::bind(&ServerApp::onMessage, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3));
  server_->start();
  LOG_INFO << "游戏服务监听 " << config_.listenAddr << ":" << config_.port
           << "（IO 线程数 " << config_.ioThreads << "）";

  // webbench 压测用的 HTTP 端点：独立的 EventLoopThread，避免拖慢游戏连接。
  //
  // 注意（踩坑记录）：TcpServer::start() 内部会调用
  // EventLoopThreadPool::start()，后者断言 “当前线程就是 base loop 线程”。
  // 因此 HTTP 服务的**构造与启动都必须投递到它自己的 loop 线程**里完成，
  // 否则会触发 EventLoop::abortNotInLoopThread 直接 core dump。
  // 这里用 CountDownLatch 等待投递的任务执行完毕，保证 start() 返回时
  // HTTP 端口已经真正处于 listen 状态。
  httpThread_.reset(new muduo::net::EventLoopThread());
  muduo::net::EventLoop* httpLoop = httpThread_->startLoop();
  muduo::CountDownLatch latch(1);
  httpLoop->runInLoop([this, httpLoop, &latch]() {
    httpServer_.reset(new HttpStatusServer(
        httpLoop, muduo::net::InetAddress(config_.listenAddr, config_.httpPort), "http-bench",
        std::bind(&ServerApp::statsJson, this)));
    httpServer_->start();
    latch.countDown();
  });
  latch.wait();
  LOG_INFO << "压测端点监听 " << config_.listenAddr << ":" << config_.httpPort
           << "（GET /health, /stats）";
}

SessionPtr ServerApp::sessionOf(const muduo::net::TcpConnectionPtr& conn) const {
  const boost::any& ctx = conn->getContext();
  if (ctx.empty()) {
    return SessionPtr();
  }
  return boost::any_cast<SessionPtr>(ctx);
}

// ===========================================================================
// 连接生命周期
// ===========================================================================
void ServerApp::onConnection(const muduo::net::TcpConnectionPtr& conn) {
  if (conn->connected()) {
    conn->setTcpNoDelay(true);  // 交互式对局，关掉 Nagle 降低指令延迟
    SessionPtr session(new Session(conn, this));
    // 连接持有 Session（通过 boost::any），Session 只持有连接的 weak_ptr，
    // 因此不会形成循环引用。
    conn->setContext(session);
    ++totalConnections_;
    LOG_INFO << conn->peerAddress().toIpPort() << " connected";
    session->send(std::string(kEvSys) + " 欢迎使用 Gomoku（muduo 版）。请先 REGISTER <昵称> <密码> 或 LOGIN <昵称> <密码>");
  } else {
    SessionPtr session = sessionOf(conn);
    if (session) {
      onDisconnect(session);
    }
    LOG_INFO << conn->peerAddress().toIpPort() << " disconnected";
  }
}

void ServerApp::onDisconnect(const SessionPtr& session) {
  // 1) 先处理房间：对局中掉线 = 认输，并可能产生一局需要落库的结果。
  GameOverInfo over;
  rooms_.onDisconnect(session, &over);
  if (over.valid) {
    onGameOver(over);
  }
  // 2) 再从在线表移除昵称，允许别人用同一昵称登录。
  if (!session->nickname().empty()) {
    removeOnline(session->nickname());
  }
}

void ServerApp::onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf,
                          muduo::Timestamp) {
  SessionPtr session = sessionOf(conn);
  if (!session) {
    return;
  }

  // 文本行协议：按 '\n' 切分。findEOL() 返回内部缓冲区的指针，
  // 因此在 retrieveUntil 之前必须先把这一行拷贝成 std::string。
  while (true) {
    const char* eol = buf->findEOL();
    if (eol == NULL) {
      break;
    }
    std::string line(buf->peek(), eol);
    buf->retrieveUntil(eol + 1);
    if (!line.empty() && line[line.size() - 1] == '\r') {
      line.resize(line.size() - 1);
    }
    session->handleLine(line);
    if (!conn->connected()) {
      return;  // 处理过程中连接已被关闭（QUIT / 协议错误）
    }
  }

  // 防御：单行超过上限（说明对端不是正常客户端或存在攻击意图）直接断开。
  if (buf->readableBytes() > kMaxLineBytes) {
    session->sendError(kErrLineTooLong, "单行超过 1024 字节，连接将被关闭");
    conn->shutdown();
  }
}

// ===========================================================================
// 在线表
// ===========================================================================
bool ServerApp::addOnline(const std::string& nickname, const SessionPtr& session) {
  std::lock_guard<std::mutex> lock(onlineMutex_);
  std::map<std::string, SessionWeakPtr>::iterator it = online_.find(nickname);
  if (it != online_.end() && !it->second.expired()) {
    return false;  // 该昵称已有活跃连接
  }
  online_[nickname] = session;
  const unsigned long long n = static_cast<unsigned long long>(online_.size());
  if (n > peakOnline_) {
    peakOnline_ = n;
  }
  return true;
}

void ServerApp::removeOnline(const std::string& nickname) {
  std::lock_guard<std::mutex> lock(onlineMutex_);
  std::map<std::string, SessionWeakPtr>::iterator it = online_.find(nickname);
  if (it != online_.end() && it->second.expired()) {
    online_.erase(it);
  } else if (it != online_.end()) {
    online_.erase(it);
  }
}

SessionPtr ServerApp::findOnline(const std::string& nickname) const {
  std::lock_guard<std::mutex> lock(onlineMutex_);
  std::map<std::string, SessionWeakPtr>::const_iterator it = online_.find(nickname);
  if (it == online_.end()) {
    return SessionPtr();
  }
  return it->second.lock();
}

// ===========================================================================
// 注册 / 登录
// ===========================================================================
void ServerApp::handleRegister(Session* session, const std::vector<std::string>& args) {
  if (session->authed()) {
    session->sendError(kErrAlreadyAuthed, "你已经登录，无需再注册");
    return;
  }
  if (args.size() != 2) {
    session->sendError(kErrBadArg, "用法: REGISTER <昵称> <密码>");
    return;
  }
  const std::string& nickname = args[0];
  const std::string& password = args[1];

  if (!isValidNickname(nickname)) {
    session->sendError(kErrBadArg, "昵称需为 2~16 个字母/数字/下划线，且以字母或数字开头");
    return;
  }
  if (!isValidPassword(password)) {
    session->sendError(kErrBadArg, "密码需为 4~32 个可打印字符且不含空格");
    return;
  }
  // 先走注册：昵称已被注册时 registerUser 会返回 kNameTaken，
  // 这比"该昵称已有连接在线"更符合用户预期（重名就是重名）。
  UserStats stats;
  std::string err;
  const AuthStatus status = auth_.registerUser(nickname, password, &stats, &err);
  if (status == AuthStatus::kNameTaken) {
    session->sendError(kErrNameTaken, "昵称已被注册，请换一个");
    return;
  }
  if (status == AuthStatus::kDbError) {
    session->sendError(kErrDbUnavailable, "数据库不可用: " + err);
    return;
  }

  // 注册成功后再抢在线名额（并发注册时 addOnline 仍可能失败，属正常兜底）。
  SessionPtr self = session->shared_from_this();
  if (!addOnline(nickname, self)) {
    session->sendError(kErrAlreadyOnline, "该昵称已有连接在线");
    return;
  }
  session->setAuthed(stats.id, nickname);
  LOG_INFO << "注册并登录成功: " << nickname << " (id=" << stats.id << ")";
  session->send(std::string(kEvAuth) + " " + nickname + " " + std::to_string(stats.games) + " " +
                std::to_string(stats.wins) + " " + stats.winRate);
  session->send(std::string(kEvSys) + " 注册成功，使用 JOIN <房间号> 进入房间（不存在会自动创建）");
}

void ServerApp::handleLogin(Session* session, const std::vector<std::string>& args) {
  if (session->authed()) {
    session->sendError(kErrAlreadyAuthed, "你已经登录");
    return;
  }
  if (args.size() != 2) {
    session->sendError(kErrBadArg, "用法: LOGIN <昵称> <密码>");
    return;
  }
  const std::string& nickname = args[0];
  const std::string& password = args[1];

  if (!isValidNickname(nickname) || !isValidPassword(password)) {
    session->sendError(kErrBadArg, "昵称或密码格式不合法");
    return;
  }
  if (findOnline(nickname)) {
    session->sendError(kErrAlreadyOnline, "该昵称已有连接在线");
    return;
  }

  UserStats stats;
  std::string err;
  const AuthStatus status = auth_.login(nickname, password, &stats, &err);
  if (status == AuthStatus::kNoSuchUser) {
    session->sendError(kErrNoSuchUser, "用户不存在，请先注册");
    return;
  }
  if (status == AuthStatus::kBadPassword) {
    session->sendError(kErrBadPassword, "密码错误");
    return;
  }
  if (status == AuthStatus::kDbError) {
    session->sendError(kErrDbUnavailable, "数据库不可用: " + err);
    return;
  }

  SessionPtr self = session->shared_from_this();
  if (!addOnline(nickname, self)) {
    session->sendError(kErrAlreadyOnline, "该昵称已有连接在线");
    return;
  }
  session->setAuthed(stats.id, nickname);
  LOG_INFO << "登录成功: " << nickname << " (" << stats.games << " 场 " << stats.wins << " 胜)";
  session->send(std::string(kEvAuth) + " " + nickname + " " + std::to_string(stats.games) + " " +
                std::to_string(stats.wins) + " " + stats.winRate);
  session->send(std::string(kEvSys) + " 欢迎回来，使用 JOIN <房间号> 进入房间");
}

// ===========================================================================
// 房间命令
// ===========================================================================
void ServerApp::handleJoin(Session* session, const std::vector<std::string>& args) {
  if (args.size() != 1) {
    session->sendError(kErrBadArg, "用法: JOIN <房间号>");
    return;
  }
  if (!isValidRoomId(args[0])) {
    session->sendError(kErrBadArg, "房间号需为 1~16 个字母或数字");
    return;
  }
  RoomError err;
  if (!rooms_.join(session->shared_from_this(), args[0], &err)) {
    session->sendError(err.code.c_str(), err.message);
    return;
  }
  session->sendOk("已进入房间 " + args[0]);
}

void ServerApp::handleLeave(Session* session) {
  if (session->roomId().empty()) {
    session->sendError(kErrNotInRoom, "你还没有进入任何房间");
    return;
  }
  GameOverInfo over;
  rooms_.leave(session->shared_from_this(), &over);
  session->sendOk("已离开房间");
  if (over.valid) {
    onGameOver(over);
  }
}

void ServerApp::handleSit(Session* session) {
  RoomError err;
  if (!rooms_.sit(session->shared_from_this(), &err)) {
    session->sendError(err.code.c_str(), err.message);
  }
}

void ServerApp::handleStand(Session* session) {
  GameOverInfo over;
  RoomError err;
  if (!rooms_.stand(session->shared_from_this(), &err, &over)) {
    session->sendError(err.code.c_str(), err.message);
    return;
  }
  if (over.valid) {
    onGameOver(over);
  }
}

void ServerApp::handleReady(Session* session) {
  RoomError err;
  if (!rooms_.setReady(session->shared_from_this(), &err)) {
    session->sendError(err.code.c_str(), err.message);
  }
}

void ServerApp::handleMove(Session* session, const std::vector<std::string>& args) {
  if (args.size() != 2) {
    session->sendError(kErrBadArg, "用法: MOVE <列 1-15> <行 1-15>");
    return;
  }
  int x = 0;
  int y = 0;
  if (!parseCoordinates(args[0], args[1], &x, &y)) {
    session->sendError(kErrBadArg, "坐标必须是 1~15 之间的整数");
    return;
  }
  GameOverInfo over;
  RoomError err;
  if (!rooms_.playMove(session->shared_from_this(), x, y, &err, &over)) {
    session->sendError(err.code.c_str(), err.message);
    return;
  }
  if (over.valid) {
    onGameOver(over);
  }
}

void ServerApp::handleChat(Session* session, const std::string& text) {
  if (text.size() > kMaxChatBytes) {
    session->sendError(kErrBadArg, "聊天内容过长（上限 " + std::to_string(kMaxChatBytes) + " 字节）");
    return;
  }
  RoomError err;
  if (!rooms_.chat(session->shared_from_this(), text, &err)) {
    session->sendError(err.code.c_str(), err.message);
    return;
  }
  ++totalChats_;
}

void ServerApp::handleBoard(Session* session) {
  rooms_.sendBoard(session->shared_from_this());
}

void ServerApp::handleWho(Session* session) {
  rooms_.sendWho(session->shared_from_this());
}

void ServerApp::handleRooms(Session* session) {
  const std::string payload = rooms_.roomListPayload();
  session->send(std::string(kEvRooms) + " " + (payload.empty() ? std::string("-") : payload));
}

void ServerApp::handleStats(Session* session) {
  UserStats stats;
  std::string err;
  if (!auth_.loadStats(session->nickname(), &stats, &err)) {
    session->sendError(kErrDbUnavailable, "读取战绩失败: " + err);
    return;
  }
  session->send(std::string(kEvInfo) + " " + std::to_string(stats.games) + " " +
                std::to_string(stats.wins) + " " + stats.winRate);
}

// ===========================================================================
// 对局结束后的数据库写入与战绩推送
// ===========================================================================
void ServerApp::onGameOver(const GameOverInfo& info) {
  if (!info.valid) {
    return;
  }
  ++totalGames_;
  LOG_INFO << "对局结束 room=" << info.roomId << " " << info.black << "(黑) vs " << info.white
           << "(白) result=" << info.result << " reason=" << info.reason
           << " moves=" << info.moves;

  std::string err;
  if (!auth_.recordResult(info.roomId, info.black, info.white, info.result, info.reason,
                          info.moves, &err)) {
    LOG_ERROR << "写入战绩失败: " << err;
  }
  // 无论入库成功与否，都把最新战绩推给两位当事人（读库失败时下面会打印日志）。
  sendInfo(info.black);
  sendInfo(info.white);
}

void ServerApp::sendInfo(const std::string& nickname) {
  if (nickname.empty()) {
    return;
  }
  SessionPtr session = findOnline(nickname);
  if (!session) {
    return;  // 对端已断开
  }
  UserStats stats;
  std::string err;
  if (!auth_.loadStats(nickname, &stats, &err)) {
    session->send(std::string(kEvSys) + " 读取战绩失败: " + err);
    return;
  }
  session->send(std::string(kEvInfo) + " " + std::to_string(stats.games) + " " +
                std::to_string(stats.wins) + " " + stats.winRate);
}

// ===========================================================================
// 统计端点
// ===========================================================================
std::string ServerApp::statsJson() {
  std::size_t online = 0;
  {
    std::lock_guard<std::mutex> lock(onlineMutex_);
    for (std::map<std::string, SessionWeakPtr>::const_iterator it = online_.begin();
         it != online_.end(); ++it) {
      if (!it->second.expired()) {
        ++online;
      }
    }
  }
  std::string dbErr;
  const long long users = auth_.userCount(&dbErr);
  const long long uptime = static_cast<long long>(std::time(NULL) - startedAt_);

  char buf[512];
  std::snprintf(buf, sizeof buf,
                "{\"status\":\"ok\",\"uptime_sec\":%lld,\"connections_total\":%llu,"
                "\"online\":%zu,\"peak_online\":%llu,\"rooms\":%zu,\"playing\":%zu,"
                "\"games_total\":%llu,\"chats_total\":%llu,\"registered_users\":%lld,"
                "\"db\":\"%s\",\"io_threads\":%d}\n",
                uptime, totalConnections_.load(), online, peakOnline_.load(), rooms_.roomCount(),
                rooms_.playingCount(), totalGames_.load(), totalChats_.load(), users,
                db_.connected() ? "up" : "down", config_.ioThreads);
  return std::string(buf);
}

}  // namespace gomoku
