#include "server/GomokuGame.h"

namespace gomoku {

namespace {
// 四条检查轴：横向、纵向、主对角线（左上-右下）、副对角线（右上-左下）
const int kDirections[4][2] = {
    {1, 0},
    {0, 1},
    {1, 1},
    {1, -1},
};
}  // namespace

// 注意：构造出来的是“空棋盘且**未开局**”的状态。
// Room 在 startGame() 里调用 reset() 才真正开始一局，
// 否则房间刚创建时就会误判为“对局进行中”，导致无法上座。
GomokuGame::GomokuGame()
    : running_(false),
      current_(kBlack),
      moves_(0),
      lastX_(0),
      lastY_(0),
      result_(kNone) {
  clearBoard();
}

void GomokuGame::clearBoard() {
  for (int y = 0; y < kBoardSize; ++y) {
    for (int x = 0; x < kBoardSize; ++x) {
      board_[y][x] = kEmpty;
    }
  }
}

void GomokuGame::reset() {
  clearBoard();
  running_ = true;
  current_ = kBlack;  // 黑先
  moves_ = 0;
  lastX_ = 0;
  lastY_ = 0;
  result_ = kNone;
}

bool GomokuGame::isEmpty(int x, int y) const {
  return inRange(x, y) && board_[y - 1][x - 1] == kEmpty;
}

int GomokuGame::cellAt(int x, int y) const {
  if (!inRange(x, y)) {
    return kEmpty;
  }
  return board_[y - 1][x - 1];
}

bool GomokuGame::place(int x, int y) {
  // 防御性校验：任何一项不满足都不改变状态。
  if (!running_ || !inRange(x, y)) {
    return false;
  }
  if (board_[y - 1][x - 1] != kEmpty) {
    return false;
  }

  board_[y - 1][x - 1] = current_;
  ++moves_;
  lastX_ = x;
  lastY_ = y;

  // 先判胜负（>=5 连即胜）；再判是否下满（和棋）；否则换手。
  if (checkFive(x, y)) {
    result_ = (current_ == kBlack) ? kBlackWin : kWhiteWin;
    running_ = false;
    return true;
  }
  if (moves_ == kCellCount) {
    result_ = kDraw;
    running_ = false;
    return true;
  }
  current_ = (current_ == kBlack) ? kWhite : kBlack;
  return true;
}

bool GomokuGame::checkFive(int x, int y) const {
  const int who = board_[y - 1][x - 1];
  if (who == kEmpty) {
    return false;
  }

  for (int d = 0; d < 4; ++d) {
    const int dx = kDirections[d][0];
    const int dy = kDirections[d][1];

    // 从落子点出发：先向正方向数，再向反方向数，最后合并计数。
    int count = 1;
    for (int step = 1;; ++step) {
      const int nx = x + dx * step;
      const int ny = y + dy * step;
      if (!inRange(nx, ny) || board_[ny - 1][nx - 1] != who) {
        break;
      }
      ++count;
    }
    for (int step = 1;; ++step) {
      const int nx = x - dx * step;
      const int ny = y - dy * step;
      if (!inRange(nx, ny) || board_[ny - 1][nx - 1] != who) {
        break;
      }
      ++count;
    }

    if (count >= 5) {
      return true;
    }
  }
  return false;
}

std::string GomokuGame::boardString() const {
  std::string out;
  out.reserve(static_cast<std::size_t>(kCellCount));
  for (int y = 0; y < kBoardSize; ++y) {
    for (int x = 0; x < kBoardSize; ++x) {
      switch (board_[y][x]) {
        case kBlack:
          out.push_back('X');
          break;
        case kWhite:
          out.push_back('O');
          break;
        default:
          out.push_back('.');
          break;
      }
    }
  }
  return out;
}

}  // namespace gomoku
