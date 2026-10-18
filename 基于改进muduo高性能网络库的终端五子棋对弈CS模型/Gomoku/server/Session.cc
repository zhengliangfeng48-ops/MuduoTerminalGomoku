#include "server/Session.h"

#include "common/Protocol.h"
#include "muduo/base/Logging.h"
#include "server/ServerApp.h"

namespace gomoku {

Session::Session(const muduo::net::TcpConnectionPtr& conn, ServerApp* app)
    : conn_(conn), app_(app), authed_(false), userId_(-1) {}

void Session::send(const std::string& line) {
  muduo::net::TcpConnectionPtr conn = conn_.lock();
  if (!conn || !conn->connected()) {
    // 连接已断开：直接丢弃。这里不能报错，因为广播场景下目标连接断开是常态。
    return;
  }
  std::string out = line;
  out.push_back('\n');
  conn->send(out);
}

void Session::sendError(const char* code, const std::string& message) {
  send(std::string(kEvErr) + " " + code + " " + message);
}

void Session::sendOk(const std::string& message) {
  send(std::string(kEvOk) + " " + message);
}

void Session::close() {
  muduo::net::TcpConnectionPtr conn = conn_.lock();
  if (conn) {
    conn->shutdown();
  }
}

void Session::setAuthed(long long userId, const std::string& nickname) {
  authed_ = true;
  userId_ = userId;
  nickname_ = nickname;
}

void Session::handleLine(const std::string& rawLine) {
  const std::string line = trim(rawLine);
  if (line.empty()) {
    return;  // 空行直接忽略，容忍客户端多发换行
  }

  // 拆出命令与参数。命令不区分大小写（客户端统一发大写，手工调试时容错）。
  const std::size_t sp = line.find_first_of(" \t");
  std::string cmd = (sp == std::string::npos) ? line : line.substr(0, sp);
  std::string rest = (sp == std::string::npos) ? std::string() : trim(line.substr(sp + 1));
  const std::vector<std::string> args = splitWhitespace(rest);

  dispatchCommand(cmd, rest, args);
}

void Session::dispatchCommand(const std::string& cmd, const std::string& rest,
                             const std::vector<std::string>& args) {
  // PING / QUIT 不要求登录
  if (equalsIgnoreCase(cmd, kCmdPing)) {
    send(kEvPong);
    return;
  }
  if (equalsIgnoreCase(cmd, kCmdQuit)) {
    sendOk("bye");
    close();
    return;
  }

  // 注册 / 登录
  if (equalsIgnoreCase(cmd, kCmdRegister)) {
    app_->handleRegister(this, args);
    return;
  }
  if (equalsIgnoreCase(cmd, kCmdLogin)) {
    app_->handleLogin(this, args);
    return;
  }

  // 其它命令都必须先认证
  if (!authed_) {
    sendError(kErrNotLogin, "请先 REGISTER <昵称> <密码> 或 LOGIN <昵称> <密码>");
    return;
  }

  if (equalsIgnoreCase(cmd, kCmdJoin)) {
    app_->handleJoin(this, args);
  } else if (equalsIgnoreCase(cmd, kCmdLeave)) {
    app_->handleLeave(this);
  } else if (equalsIgnoreCase(cmd, kCmdSit)) {
    app_->handleSit(this);
  } else if (equalsIgnoreCase(cmd, kCmdStand)) {
    app_->handleStand(this);
  } else if (equalsIgnoreCase(cmd, kCmdReady)) {
    app_->handleReady(this);
  } else if (equalsIgnoreCase(cmd, kCmdMove)) {
    app_->handleMove(this, args);
  } else if (equalsIgnoreCase(cmd, kCmdChat)) {
    // 聊天正文取 "CHAT " 之后的原始内容（允许含空格）
    app_->handleChat(this, rest);
  } else if (equalsIgnoreCase(cmd, kCmdBoard)) {
    app_->handleBoard(this);
  } else if (equalsIgnoreCase(cmd, kCmdWho)) {
    app_->handleWho(this);
  } else if (equalsIgnoreCase(cmd, kCmdRooms)) {
    app_->handleRooms(this);
  } else if (equalsIgnoreCase(cmd, kCmdStats)) {
    app_->handleStats(this);
  } else {
    sendError(kErrBadCmd, "未知命令: " + cmd);
  }
}

}  // namespace gomoku
