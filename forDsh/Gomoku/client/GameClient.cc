#include "client/GameClient.h"

#include <cctype>
#include <cstdlib>
#include <vector>

#include "common/Protocol.h"
#include "muduo/base/Logging.h"
#include "muduo/net/InetAddress.h"

namespace gomoku {

GameClient::Options::Options()
    : host("127.0.0.1"), port(9527), doRegister(false), color(true) {}

GameClient::GameClient(muduo::net::EventLoop* loop, const Options& options)
    : loop_(loop),
      options_(options),
      client_(loop, muduo::net::InetAddress(options.host, options.port), "gomoku-client"),
      authed_(false) {
  ui_.setColor(options_.color);
  client_.setConnectionCallback(
      std::bind(&GameClient::onConnection, this, std::placeholders::_1));
  client_.setMessageCallback(
      std::bind(&GameClient::onMessage, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3));
}

void GameClient::connect() {
  client_.connect();
}

void GameClient::sendLine(const std::string& line) {
  if (!conn_ || !conn_->connected()) {
    ui_.printError("NOT_CONNECTED", "还没有连接到服务器");
    return;
  }
  conn_->send(line + "\n");
}

// ===========================================================================
// 连接与收包
// ===========================================================================
void GameClient::onConnection(const muduo::net::TcpConnectionPtr& conn) {
  if (conn->connected()) {
    conn_ = conn;
    ui_.printSystem("已连接 " + conn->peerAddress().toIpPort());
    // 连上后立刻用启动时收集到的昵称/密码完成注册或登录
    if (options_.doRegister) {
      sendLine(std::string(kCmdRegister) + " " + options_.nickname + " " + options_.password);
    } else {
      sendLine(std::string(kCmdLogin) + " " + options_.nickname + " " + options_.password);
    }
  } else {
    conn_.reset();
    ui_.printEvent("\n连接已断开");
    loop_->quit();
  }
}

void GameClient::onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf,
                           muduo::Timestamp) {
  (void)conn;
  // 与服务器一致：按 '\n' 切分，先拷贝再 retrieve。
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
    onLine(line);
  }
}

// ===========================================================================
// 协议消息分发
// ===========================================================================
void GameClient::onLine(const std::string& line) {
  const std::string message = trim(line);
  if (message.empty()) {
    return;
  }
  const std::size_t sp = message.find(' ');
  const std::string head = (sp == std::string::npos) ? message : message.substr(0, sp);
  const std::string body = (sp == std::string::npos) ? std::string() : message.substr(sp + 1);

  if (head == kEvOk) {
    ui_.printSystem(body);
  } else if (head == kEvErr) {
    // ERR <CODE> <message...>
    const std::size_t sp2 = body.find(' ');
    const std::string code = (sp2 == std::string::npos) ? body : body.substr(0, sp2);
    const std::string text = (sp2 == std::string::npos) ? std::string() : body.substr(sp2 + 1);
    ui_.printError(code, text);
  } else if (head == kEvAuth) {
    // AUTH <nick> <games> <wins> <rate>
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 4) {
      ui_.setMe(f[0]);
      ui_.updateStats(std::atoi(f[1].c_str()), std::atoi(f[2].c_str()), f[3]);
      authed_ = true;
      ui_.printSystem("登录成功：" + f[0] + "，战绩 " + f[2] + "/" + f[1] + " (" + f[3] + ")");
      ui_.printSystem("用 /join <房间号> 进入房间（不存在会自动创建），/help 查看命令");
    }
  } else if (head == kEvRoom) {
    // ROOM <roomId> <state> <black> <white> <you>
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 5) {
      ui_.updateRoom(f[0], f[1], f[2], f[3], f[4]);
      ui_.printSystem("已进入房间 " + f[0] + "（状态 " + f[1] + "）");
      ui_.printBoard();
      ui_.printPrompt();
    }
  } else if (head == kEvSeats) {
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 3) {
      ui_.updateSeats(f[0], f[1], f[2]);
    }
  } else if (head == kEvJoined) {
    ui_.printSystem(body + " 进入了房间");
  } else if (head == kEvLeft) {
    ui_.printSystem(body + " 离开了房间");
  } else if (head == kEvUsers) {
    ui_.setUsers(body);
  } else if (head == kEvStart) {
    // START <black> <white>
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 2) {
      ui_.setTurn(f[0]);
      ui_.printSystem("对局开始！黑: " + f[0] + "  白: " + f[1] + "（黑先）");
    }
  } else if (head == kEvTurn) {
    ui_.setTurn(body);
    // 轮次信息已经体现在棋盘头部，这里不再单独打印一行，避免刷屏
  } else if (head == kEvPlaced) {
    // PLACED <x> <y> <nick> <B|W>
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 4) {
      const int x = std::atoi(f[0].c_str());
      const int y = std::atoi(f[1].c_str());
      char col = static_cast<char>('A' + x - 1);
      // 下一手该谁：黑落子后轮到白，反之亦然。
      // 这里先本地推算，好让紧接着到达的 BOARD 能显示正确的"轮到"；
      // 服务端随后还会发 TURN，两者一致。
      ui_.setTurn(f[3] == "B" ? ui_.whitePlayer() : ui_.blackPlayer());
      ui_.printSystem(f[2] + " 落子 " + std::string(1, col) + std::to_string(y));
    }
  } else if (head == kEvBoard) {
    // BOARD <225 chars> <lastX> <lastY>
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 3) {
      ui_.updateBoard(f[0], std::atoi(f[1].c_str()), std::atoi(f[2].c_str()));
      ui_.printBoard();
      ui_.printPrompt();
    }
  } else if (head == kEvOver) {
    // OVER <WON|DRAW> <winner> <reason>
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 3) {
      std::string text = "对局结束：";
      if (f[0] == "DRAW") {
        text += "和棋";
      } else {
        text += "胜者 " + f[1];
      }
      text += "（" + f[2] + "）";
      ui_.printSystem(text);
      ui_.printSystem("可以使用 /ready 准备下一局，或 /stand 起身让位");
    }
  } else if (head == kEvInfo) {
    std::vector<std::string> f = splitWhitespace(body);
    if (f.size() >= 3) {
      ui_.updateStats(std::atoi(f[0].c_str()), std::atoi(f[1].c_str()), f[2]);
      ui_.printSystem("我的战绩：场次 " + f[0] + "，胜场 " + f[1] + "，胜率 " + f[2]);
    }
  } else if (head == kEvChat) {
    const std::size_t sp2 = body.find(' ');
    const std::string from = (sp2 == std::string::npos) ? body : body.substr(0, sp2);
    const std::string text = (sp2 == std::string::npos) ? std::string() : body.substr(sp2 + 1);
    ui_.printChat(from, text);
  } else if (head == kEvSys) {
    ui_.printSystem(body);
  } else if (head == kEvRooms) {
    ui_.printRooms(body);
  } else if (head == kEvPong) {
    ui_.printEvent("PONG");
  } else {
    ui_.printEvent(body.empty() ? message : message);
  }
}

// ===========================================================================
// 用户输入
// ===========================================================================
void GameClient::handleInput(const std::string& raw) {
  const std::string line = trim(raw);
  if (line.empty()) {
    ui_.printPrompt();
    return;
  }
  if (!authed_) {
    ui_.printError("WAIT", "正在等待登录响应…");
    return;
  }

  if (line[0] == '/') {
    const std::size_t sp = line.find(' ');
    const std::string cmd = (sp == std::string::npos) ? line : line.substr(0, sp);
    const std::string rest = (sp == std::string::npos) ? std::string() : trim(line.substr(sp + 1));
    handleLocalCommand(cmd, rest);
    return;
  }

  // 不是 / 开头的：先看是不是落子坐标，否则当聊天内容
  int x = 0;
  int y = 0;
  if (parseMoveInput(line, &x, &y)) {
    sendLine(std::string(kCmdMove) + " " + std::to_string(x) + " " + std::to_string(y));
    return;
  }
  sendLine(std::string(kCmdChat) + " " + line);
}

void GameClient::handleLocalCommand(const std::string& cmd, const std::string& rest) {
  if (cmd == "/help" || cmd == "/?") {
    ui_.printHelp();
  } else if (cmd == "/join") {
    if (rest.empty()) {
      ui_.printError("BAD_ARG", "用法: /join <房间号>");
      return;
    }
    sendLine(std::string(kCmdJoin) + " " + rest);
  } else if (cmd == "/leave") {
    sendLine(kCmdLeave);
  } else if (cmd == "/sit") {
    sendLine(kCmdSit);
  } else if (cmd == "/stand") {
    sendLine(kCmdStand);
  } else if (cmd == "/ready") {
    sendLine(kCmdReady);
  } else if (cmd == "/board" || cmd == "/show") {
    sendLine(kCmdBoard);
  } else if (cmd == "/who") {
    sendLine(kCmdWho);
  } else if (cmd == "/rooms") {
    sendLine(kCmdRooms);
  } else if (cmd == "/stats") {
    sendLine(kCmdStats);
  } else if (cmd == "/chat") {
    if (rest.empty()) {
      ui_.printError("BAD_ARG", "用法: /chat <文本>");
      return;
    }
    sendLine(std::string(kCmdChat) + " " + rest);
  } else if (cmd == "/move") {
    int x = 0;
    int y = 0;
    if (!parseMoveInput(rest, &x, &y)) {
      ui_.printError("BAD_ARG", "用法: /move <列 1-15> <行 1-15> 或 /move H8");
      return;
    }
    sendLine(std::string(kCmdMove) + " " + std::to_string(x) + " " + std::to_string(y));
  } else if (cmd == "/clear") {
    std::printf("\033[2J\033[H");
    ui_.printBoard();
  } else if (cmd == "/quit" || cmd == "/exit") {
    sendLine(kCmdQuit);
    conn_->shutdown();
  } else {
    ui_.printError("BAD_CMD", "未知命令 " + cmd + "，输入 /help 查看帮助");
  }
}

bool GameClient::parseMoveInput(const std::string& text, int* x, int* y) {
  if (text.empty()) {
    return false;
  }
  // 形式一：字母列 + 数字行，例如 H8 / h8
  if (std::isalpha(static_cast<unsigned char>(text[0]))) {
    const char col = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    if (col < 'A' || col > 'O') {
      return false;
    }
    const std::string digits = text.substr(1);
    if (digits.empty() || digits.size() > 2) {
      return false;
    }
    for (std::size_t i = 0; i < digits.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(digits[i]))) {
        return false;
      }
    }
    const int row = std::atoi(digits.c_str());
    if (row < 1 || row > kBoardSize) {
      return false;
    }
    *x = col - 'A' + 1;
    *y = row;
    return true;
  }

  // 形式二：两个数字，例如 "8 8" 或 "8,8"
  std::string normalized = text;
  for (std::size_t i = 0; i < normalized.size(); ++i) {
    if (normalized[i] == ',') {
      normalized[i] = ' ';
    }
  }
  const std::vector<std::string> parts = splitWhitespace(normalized);
  if (parts.size() != 2) {
    return false;
  }
  return parseCoordinates(parts[0], parts[1], x, y);
}

}  // namespace gomoku
