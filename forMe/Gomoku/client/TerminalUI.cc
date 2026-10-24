#include "client/TerminalUI.h"

#include <cstdio>

namespace gomoku {

namespace {
const char* kReset = "\033[0m";
const char* kCyan = "\033[36m";
const char* kYellow = "\033[33m";
const char* kGreen = "\033[32m";
const char* kRed = "\033[31m";
const char* kBold = "\033[1m";
}  // namespace

TerminalUI::TerminalUI()
    : color_(true),
      lastX_(0),
      lastY_(0),
      games_(0),
      wins_(0),
      rate_("0.0000") {}

std::string TerminalUI::colorize(const char* code, const std::string& text) const {
  if (!color_) {
    return text;
  }
  return std::string(code) + text + kReset;
}

char TerminalUI::symbolOf(char cell) {
  if (cell == 'X' || cell == 'x') {
    return 'X';
  }
  if (cell == 'O' || cell == 'o') {
    return 'O';
  }
  return '.';
}

bool TerminalUI::atSeat() const {
  return me_ == black_ || me_ == white_;
}

void TerminalUI::updateRoom(const std::string& roomId, const std::string& state,
                            const std::string& black, const std::string& white,
                            const std::string& you) {
  roomId_ = roomId;
  state_ = state;
  black_ = black;
  white_ = white;
  if (!you.empty() && you != "-") {
    me_ = you;
  }
}

void TerminalUI::updateSeats(const std::string& black, const std::string& white,
                             const std::string& state) {
  black_ = black;
  white_ = white;
  state_ = state;
}

void TerminalUI::updateBoard(const std::string& board, int lastX, int lastY) {
  board_ = board;
  lastX_ = lastX;
  lastY_ = lastY;
}

void TerminalUI::updateStats(int games, int wins, const std::string& rate) {
  games_ = games;
  wins_ = wins;
  rate_ = rate;
}

void TerminalUI::printPrompt() {
  // 不加前置换行：上一条输出总是以 '\n' 结尾，这样不会出现空行。
  std::printf("%s", colorize(kBold, "> ").c_str());
  std::fflush(stdout);
}

void TerminalUI::printEvent(const std::string& text) {
  // \r\033[K 用于清掉用户正在输入的半行，避免与异步消息黏在一起
  std::printf("\r\033[K%s\n", text.c_str());
  printPrompt();
}

void TerminalUI::printChat(const std::string& from, const std::string& text) {
  const bool isMe = (from == me_);
  std::printf("\r\033[K%s %s\n", colorize(kGreen, "[" + from + "]").c_str(), text.c_str());
  if (isMe) {
    // 自己的消息不回显提示符以外的东西
  }
  printPrompt();
}

void TerminalUI::printSystem(const std::string& text) {
  printEvent(colorize(kCyan, "* " + text));
}

void TerminalUI::printError(const std::string& code, const std::string& message) {
  printEvent(colorize(kRed, "[错误 " + code + "] " + message));
}

void TerminalUI::printBoard() {
  std::string header = "房间 " + (roomId_.empty() ? std::string("-") : roomId_) + "  [";
  header += state_.empty() ? "未进房" : state_;
  header += "]   黑 X: ";
  header += (black_.empty() || black_ == "-") ? std::string("-") : black_;
  header += "   白 O: ";
  header += (white_.empty() || white_ == "-") ? std::string("-") : white_;
  header += "   轮到: ";
  header += turn_.empty() ? std::string("-") : turn_;
  header += "   战绩: " + std::to_string(wins_) + "/" + std::to_string(games_) + " (" + rate_ + ")";
  std::printf("%s\n", colorize(kBold, header).c_str());

  // 列号 A~O
  std::printf("    ");
  for (int x = 0; x < 15; ++x) {
    std::printf("%c ", 'A' + x);
  }
  std::printf("\n");

  const bool hasBoard = (board_.size() == 225);
  for (int y = 0; y < 15; ++y) {
    std::printf("%2d  ", y + 1);
    for (int x = 0; x < 15; ++x) {
      const char raw = hasBoard ? board_[static_cast<std::size_t>(y) * 15 + x] : '.';
      const char sym = symbolOf(raw);
      std::string cell(1, sym);
      if (lastX_ == x + 1 && lastY_ == y + 1) {
        cell = colorize(kYellow, cell);  // 高亮最后一手
      } else if (sym == 'X') {
        cell = colorize(kGreen, cell);
      } else if (sym == 'O') {
        cell = colorize(kCyan, cell);
      }
      std::printf("%s ", cell.c_str());
    }
    std::printf("\n");
  }
  if (lastX_ > 0) {
    std::printf("最后一手: %c%d\n", 'A' + lastX_ - 1, lastY_);
  }
  if (!usersCsv_.empty() && usersCsv_ != "-") {
    std::printf("房间成员: %s\n", usersCsv_.c_str());
  }
  if (atSeat()) {
    std::printf("输入 H8 或 8 8 落子；/chat 文本 聊天；/stand 起身；/help 查看全部命令\n");
  } else {
    std::printf("输入 /sit 上座（空座时）；/chat 文本 聊天；/board 重绘棋盘；/help 查看全部命令\n");
  }
}

void TerminalUI::printBanner() {
  std::printf("%s\n", colorize(kBold, "================================================").c_str());
  std::printf("%s\n", colorize(kBold, "   Gomoku 终端五子棋 —— 基于 muduo 网络库").c_str());
  std::printf("%s\n", colorize(kBold, "================================================").c_str());
}

void TerminalUI::printHelp() {
  printEvent(colorize(kBold, "命令列表"));
  printEvent("  /join <房间号>   进入房间（不存在则自动创建）");
  printEvent("  /leave           离开当前房间");
  printEvent("  /sit             上座（房间无对局且有空座时）");
  printEvent("  /stand           起身（对局中起身=认输）");
  printEvent("  /ready           上一局结束后准备下一局");
  printEvent("  H8 / 8 8         落子（列 A~O，行 1~15）");
  printEvent("  /move <列> <行>  落子的另一种写法");
  printEvent("  /chat <文本>     发聊天（对局中只有对弈双方可发）");
  printEvent("  /board           重绘棋盘；/who 房间成员；/rooms 房间列表；/stats 我的战绩");
  printEvent("  /clear           清屏；/quit 退出");
  printEvent("  直接输入非命令文本 = 发送聊天");
}

void TerminalUI::printRooms(const std::string& payload) {
  if (payload == "-" || payload.empty()) {
    printEvent("当前没有任何房间");
    return;
  }
  printEvent(colorize(kBold, "房间列表（房间号:状态:黑:白:人数）"));
  std::size_t begin = 0;
  while (begin <= payload.size()) {
    std::size_t end = payload.find('|', begin);
    if (end == std::string::npos) {
      end = payload.size();
    }
    printEvent("  " + payload.substr(begin, end - begin));
    if (end == payload.size()) {
      break;
    }
    begin = end + 1;
  }
}

}  // namespace gomoku
