// ============================================================================
// 五子棋棋盘与规则（纯逻辑，不含任何网络/数据库代码，便于单元测试）
//
// 坐标约定：x 为列、y 为行，取值范围 1..kBoardSize(15)。
// 内部用 board_[y-1][x-1] 存储，0=空、1=黑、2=白。
//
// 规则取舍：
//   * 黑先；
//   * 任意方向连续 >= 5 子即胜（不实现连珠的“长连禁手/三三禁手”，
//     属于休闲对弈的简化规则，README 中已说明）；
//   * 棋盘下满且无人五连则为和棋。
// ============================================================================
#pragma once

#include <string>

#include "common/Protocol.h"

namespace gomoku {

class GomokuGame {
 public:
  // 格子内容
  enum Cell {
    kEmpty = 0,
    kBlack = 1,
    kWhite = 2,
  };

  // 对局结果
  enum Result {
    kNone = 0,   // 尚未结束
    kBlackWin,   // 黑胜
    kWhiteWin,   // 白胜
    kDraw,       // 和棋
  };

  GomokuGame();

  // 开始新的一局：清空棋盘、黑先、running_ 置为 true。
  // 注意：构造函数只清空棋盘，**不会**把对局置为进行中。
  void reset();

  // 对局是否仍在进行。
  bool running() const { return running_; }

  // 结果（running()==false 时有效）。
  Result result() const { return result_; }

  // 当前该谁落子（kBlack / kWhite）。
  int current() const { return current_; }

  // 已落子数。
  int moves() const { return moves_; }

  // 最近一手坐标；未落子时为 0,0。
  int lastX() const { return lastX_; }
  int lastY() const { return lastY_; }

  // 坐标是否在棋盘内。
  static bool inRange(int x, int y) { return x >= 1 && x <= kBoardSize && y >= 1 && y <= kBoardSize; }

  // 该点是否为空（越界返回 false）。
  bool isEmpty(int x, int y) const;

  // 读取格子内容（越界返回 kEmpty）。
  int cellAt(int x, int y) const;

  // 落子。调用方应先校验 running()/轮次/坐标，这里再做一次防御性校验：
  // 越界、已占用、对局已结束时返回 false 且不改变状态。
  bool place(int x, int y);

  // 外部判定结束（认输/断线），不改动棋盘。
  void finish(Result r) {
    running_ = false;
    result_ = r;
  }

  // 序列化为 225 个字符（行优先），'.'=空、'X'=黑、'O'=白。
  // 用于 BOARD 事件，客户端据此重绘棋盘。
  std::string boardString() const;

 private:
  // 以 (x,y) 这枚棋子为中心，检查是否形成五连（含六连等长连）。
  // 实现：沿 4 条轴（横、竖、两条斜线）双向累加同色棋子数。
  bool checkFive(int x, int y) const;

  // 清空棋盘格子（不动 running_ / result_ 等状态）。
  void clearBoard();

  int board_[kBoardSize][kBoardSize];
  bool running_;
  int current_;
  int moves_;
  int lastX_;
  int lastY_;
  Result result_;
};

}  // namespace gomoku
