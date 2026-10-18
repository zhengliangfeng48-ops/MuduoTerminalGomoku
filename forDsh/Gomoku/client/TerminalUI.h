// ============================================================================
// 终端渲染层：把服务端推送的状态画成文字棋盘/房间信息
//
// 渲染约束：
//   * 纯文本 + ANSI 转义（颜色、清行），不依赖 ncurses；
//   * 棋盘 15x15，列用 A~O、行用 1~15，落子坐标可直接用 H8 这种记法；
//   * 所有输出都在 IO 线程里完成（stdin 线程只负责把输入投递进来），
//     因此这里不需要加锁。
// ============================================================================
#pragma once

#include <string>

namespace gomoku {

class TerminalUI {
 public:
  TerminalUI();

  void setColor(bool enabled) { color_ = enabled; }
  void setMe(const std::string& nick) { me_ = nick; }

  // 房间快照（ROOM 事件）
  void updateRoom(const std::string& roomId, const std::string& state, const std::string& black,
                  const std::string& white, const std::string& you);
  // 座位变化（SEATS 事件）
  void updateSeats(const std::string& black, const std::string& white, const std::string& state);
  // 棋盘（BOARD 事件）
  void updateBoard(const std::string& board, int lastX, int lastY);
  // 轮次（TURN 事件）
  void setTurn(const std::string& nick) { turn_ = nick; }
  // 战绩（AUTH / INFO 事件）
  void updateStats(int games, int wins, const std::string& rate);
  // 成员（USERS 事件）
  void setUsers(const std::string& csv) { usersCsv_ = csv; }
  // 房间列表（ROOMS 事件）
  void printRooms(const std::string& payload);

  // 普通事件行（聊天/系统/错误），会自动回到提示符
  void printEvent(const std::string& text);
  void printChat(const std::string& from, const std::string& text);
  void printSystem(const std::string& text);
  void printError(const std::string& code, const std::string& message);

  // 棋盘与提示符
  void printBoard();
  void printPrompt();
  void printHelp();
  void printBanner();

  const std::string& roomId() const { return roomId_; }
  const std::string& roomState() const { return state_; }
  const std::string& blackPlayer() const { return black_; }
  const std::string& whitePlayer() const { return white_; }

 private:
  std::string colorize(const char* code, const std::string& text) const;
  static char symbolOf(char cell);
  bool atSeat() const;

  bool color_;
  std::string me_;
  std::string roomId_;
  std::string state_;
  std::string black_;
  std::string white_;
  std::string board_;   // 225 个 '.'/'X'/'O'
  std::string turn_;
  std::string usersCsv_;
  int lastX_;
  int lastY_;
  int games_;
  int wins_;
  std::string rate_;
};

}  // namespace gomoku
