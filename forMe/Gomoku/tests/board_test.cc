// ============================================================================
// 棋盘与规则单元测试（不依赖任何测试框架，直接 assert 风格）
//
// 覆盖：坐标校验、四个方向五连、长连（六连）、非法落子、
//       满盘和棋、棋盘序列化、协议工具函数。
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/Protocol.h"
#include "server/GomokuGame.h"

using gomoku::GomokuGame;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      std::printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

#define CHECK_EQ_INT(a, b)                                                   \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!((a) == (b))) {                                                     \
      std::printf("  [FAIL] %s:%d  %s == %s (%d vs %d)\n", __FILE__,        \
                  __LINE__, #a, #b, static_cast<int>(a),                     \
                  static_cast<int>(b));                                      \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

#define CHECK_EQ_STR(a, b)                                                   \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!((a) == (b))) {                                                     \
      std::printf("  [FAIL] %s:%d  %s == %s (\"%s\" vs \"%s\")\n",       \
                  __FILE__, __LINE__, #a, #b, std::string(a).c_str(),        \
                  std::string(b).c_str());                                   \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

namespace {

// 交替落子的小工具：黑下一手、白下一手。
// 用法：Play(g, {{1,8},{2,8}}); 表示黑依次下这些点、白依次下这些点。
void AlternatePlace(GomokuGame* g,
                    const std::vector<std::pair<int, int>>& black,
                    const std::vector<std::pair<int, int>>& white) {
  const std::size_t n = black.size() > white.size() ? black.size() : white.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (i < black.size()) {
      const bool ok = g->place(black[i].first, black[i].second);
      CHECK(ok);
      if (!g->running()) {
        return;
      }
    }
    if (i < white.size()) {
      const bool ok = g->place(white[i].first, white[i].second);
      CHECK(ok);
      if (!g->running()) {
        return;
      }
    }
  }
}

// ------------------------- 1. 坐标与初始状态 -------------------------
void TestBasics() {
  std::printf("[1] 坐标校验与初始状态\n");
  GomokuGame g;
  // 新建的棋盘是“未开局”状态：不能落子（否则房间刚创建就会被当成对局中）
  CHECK(!g.running());
  CHECK(g.result() == GomokuGame::kNone);
  CHECK(!g.place(1, 1));
  g.reset();
  CHECK(g.running());
  CHECK(g.result() == GomokuGame::kNone);
  CHECK_EQ_INT(g.current(), GomokuGame::kBlack);
  CHECK_EQ_INT(g.moves(), 0);

  CHECK(GomokuGame::inRange(1, 1));
  CHECK(GomokuGame::inRange(15, 15));
  CHECK(!GomokuGame::inRange(0, 1));
  CHECK(!GomokuGame::inRange(16, 1));
  CHECK(!GomokuGame::inRange(1, 0));
  CHECK(!GomokuGame::inRange(1, 16));

  CHECK(g.isEmpty(1, 1));
  CHECK(g.place(1, 1));
  CHECK(!g.isEmpty(1, 1));
  CHECK_EQ_INT(g.cellAt(1, 1), GomokuGame::kBlack);
  CHECK_EQ_INT(g.moves(), 1);
  CHECK_EQ_INT(g.current(), GomokuGame::kWhite);
  // 重复落子必须失败
  CHECK(!g.place(1, 1));
  // 越界落子必须失败
  CHECK(!g.place(0, 5));
  CHECK(!g.place(16, 5));
  CHECK_EQ_INT(g.moves(), 1);
}

// ------------------------- 2. 横向五连 -------------------------
void TestHorizontalWin() {
  std::printf("[2] 横向五连\n");
  GomokuGame g;
  g.reset();
  // 黑：(1,8)..(5,8)；白：(1,9)..(4,9)
  AlternatePlace(&g, {{1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}},
                 {{1, 9}, {2, 9}, {3, 9}, {4, 9}});
  CHECK(!g.running());
  CHECK(g.result() == GomokuGame::kBlackWin);
  CHECK_EQ_INT(g.moves(), 9);
  CHECK_EQ_INT(g.lastX(), 5);
  CHECK_EQ_INT(g.lastY(), 8);
}

// ------------------------- 3. 纵向五连 -------------------------
void TestVerticalWin() {
  std::printf("[3] 纵向五连\n");
  GomokuGame g;
  g.reset();
  AlternatePlace(&g, {{8, 1}, {8, 2}, {8, 3}, {8, 4}, {8, 5}},
                 {{9, 1}, {9, 2}, {9, 3}, {9, 4}});
  CHECK(!g.running());
  CHECK(g.result() == GomokuGame::kBlackWin);
}

// ------------------------- 4. 两条斜线五连 -------------------------
void TestDiagonalWins() {
  std::printf("[4] 斜线五连\n");
  {
    GomokuGame g;  // 主对角线：左上 -> 右下
    g.reset();
    AlternatePlace(&g, {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}},
                   {{15, 1}, {14, 1}, {13, 1}, {12, 1}});
    CHECK(!g.running());
    CHECK(g.result() == GomokuGame::kBlackWin);
  }
  {
    GomokuGame g;  // 副对角线：右上 -> 左下
    g.reset();
    AlternatePlace(&g, {{5, 1}, {4, 2}, {3, 3}, {2, 4}, {1, 5}},
                   {{15, 15}, {14, 15}, {13, 15}, {12, 15}});
    CHECK(!g.running());
    CHECK(g.result() == GomokuGame::kBlackWin);
  }
}

// ------------------------- 5. 六连长连也算胜 -------------------------
void TestLongLine() {
  std::printf("[5] 长连（六连）\n");
  GomokuGame g;
  g.reset();
  // 先在 (1..3,5) 与 (5..6,5) 下子，中间留空 (4,5)，最后补 (4,5) 形成六连。
  // 此时白子放在第 10 行，不会干扰。
  // 注意：白子的第 10 行必须彼此隔开，否则白方会先形成五连，测试就失去意义。
  CHECK(g.place(1, 5));   // 黑
  CHECK(g.place(1, 10));  // 白
  CHECK(g.place(2, 5));   // 黑
  CHECK(g.place(3, 10));  // 白
  CHECK(g.place(3, 5));   // 黑
  CHECK(g.place(5, 10));  // 白
  CHECK(g.place(5, 5));   // 黑
  CHECK(g.place(7, 10));  // 白
  CHECK(g.place(6, 5));   // 黑
  CHECK(g.place(9, 10));  // 白
  CHECK(g.running());     // 还没有五连
  CHECK(g.place(4, 5));   // 黑补中间 -> 1..6 六连
  CHECK(!g.running());
  CHECK(g.result() == GomokuGame::kBlackWin);
}

// ------------------------- 6. 满盘和棋 -------------------------
// 采用 ((x + 2y) % 8) < 4 的染色：黑 113 子、白 112 子，
// 且四个方向的最长同色连续为 4（无五连），因此必然是满盘和棋。
// 由于 place() 是按“黑、白、黑、白……”交替着色的，
// 测试中把黑点与白点交错投喂即可复现该染色。
void TestDrawOnFullBoard() {
  std::printf("[6] 满盘和棋\n");
  std::vector<std::pair<int, int>> blackCells;
  std::vector<std::pair<int, int>> whiteCells;
  for (int y = 1; y <= gomoku::kBoardSize; ++y) {
    for (int x = 1; x <= gomoku::kBoardSize; ++x) {
      if ((x + 2 * y) % 8 < 4) {
        blackCells.push_back(std::make_pair(x, y));
      } else {
        whiteCells.push_back(std::make_pair(x, y));
      }
    }
  }
  CHECK_EQ_INT(static_cast<int>(blackCells.size()), 113);
  CHECK_EQ_INT(static_cast<int>(whiteCells.size()), 112);

  GomokuGame g;
  g.reset();
  for (std::size_t i = 0; i < blackCells.size(); ++i) {
    CHECK(g.place(blackCells[i].first, blackCells[i].second));
    if (!g.running()) {
      break;
    }
    if (i < whiteCells.size()) {
      CHECK(g.place(whiteCells[i].first, whiteCells[i].second));
      if (!g.running()) {
        break;
      }
    }
  }
  CHECK(!g.running());
  CHECK(g.result() == GomokuGame::kDraw);
  CHECK_EQ_INT(g.moves(), gomoku::kCellCount);
}

// ------------------------- 7. 外部判定结束 -------------------------
void TestFinishAndReset() {
  std::printf("[7] 认输结束与重置\n");
  GomokuGame g;
  g.reset();
  CHECK(g.place(7, 7));
  g.finish(GomokuGame::kWhiteWin);  // 模拟黑方认输
  CHECK(!g.running());
  CHECK(g.result() == GomokuGame::kWhiteWin);
  CHECK(!g.place(8, 8));  // 结束后不允许继续落子

  g.reset();
  CHECK(g.running());
  CHECK(g.result() == GomokuGame::kNone);
  CHECK_EQ_INT(g.moves(), 0);
  CHECK_EQ_INT(g.current(), GomokuGame::kBlack);
  CHECK(g.isEmpty(7, 7));
}

// ------------------------- 8. 棋盘序列化 -------------------------
void TestBoardString() {
  std::printf("[8] 棋盘序列化\n");
  GomokuGame g;
  g.reset();
  std::string s = g.boardString();
  CHECK_EQ_INT(static_cast<int>(s.size()), gomoku::kCellCount);
  for (std::size_t i = 0; i < s.size(); ++i) {
    CHECK(s[i] == '.');
  }
  // 黑下 (1,1) -> 索引 0 为 'X'；白下 (15,15) -> 索引 224 为 'O'
  CHECK(g.place(1, 1));
  CHECK(g.place(15, 15));
  s = g.boardString();
  CHECK(s[0] == 'X');
  CHECK(s[224] == 'O');
  CHECK_EQ_INT(static_cast<int>(s.size()), 225);
}

// ------------------------- 9. 协议工具函数 -------------------------
void TestProtocolHelpers() {
  std::printf("[9] 协议工具函数\n");
  CHECK_EQ_STR(gomoku::trim("  ab c \r\n"), std::string("ab c"));
  CHECK_EQ_STR(gomoku::trim(""), std::string(""));

  std::vector<std::string> t = gomoku::splitWhitespace("MOVE   8  9");
  CHECK_EQ_INT(static_cast<int>(t.size()), 3);
  if (t.size() == 3) {
    CHECK_EQ_STR(t[0], std::string("MOVE"));
    CHECK_EQ_STR(t[1], std::string("8"));
    CHECK_EQ_STR(t[2], std::string("9"));
  }

  CHECK(gomoku::startsWith("CHAT hello", "CHAT"));
  CHECK(!gomoku::startsWith("CHA", "CHAT"));
  CHECK(gomoku::equalsIgnoreCase("move", "MOVE"));
  CHECK(!gomoku::equalsIgnoreCase("move", "MOVE2"));

  CHECK(gomoku::isValidNickname("alice"));
  CHECK(gomoku::isValidNickname("a_1"));
  CHECK(!gomoku::isValidNickname("a"));            // 太短
  CHECK(!gomoku::isValidNickname("_abc"));         // 首字符不能是下划线
  CHECK(!gomoku::isValidNickname("中文名字"));      // 仅 ASCII
  CHECK(!gomoku::isValidNickname("a b"));          // 不能含空格
  CHECK(gomoku::isValidNickname("abcdefghijklmnop"));   // 16 字节
  CHECK(!gomoku::isValidNickname("abcdefghijklmnopq")); // 17 字节

  CHECK(gomoku::isValidPassword("pass1234"));
  CHECK(!gomoku::isValidPassword("abc"));     // 太短
  CHECK(!gomoku::isValidPassword("ab c1234"));  // 含空格
  CHECK(gomoku::isValidPassword("P@ss!"));

  CHECK(gomoku::isValidRoomId("1001"));
  CHECK(gomoku::isValidRoomId("abcXYZ"));
  CHECK(!gomoku::isValidRoomId(""));
  CHECK(!gomoku::isValidRoomId("1001-2"));
  CHECK(!gomoku::isValidRoomId("房间1"));

  CHECK_EQ_STR(gomoku::formatWinRate(0, 0), std::string("0.0000"));
  CHECK_EQ_STR(gomoku::formatWinRate(2, 3), std::string("0.6667"));
  CHECK_EQ_STR(gomoku::formatWinRate(3, 3), std::string("1.0000"));

  int x = 0;
  int y = 0;
  CHECK(gomoku::parseCoordinates("8", "9", &x, &y));
  CHECK_EQ_INT(x, 8);
  CHECK_EQ_INT(y, 9);
  CHECK(!gomoku::parseCoordinates("0", "9", &x, &y));
  CHECK(!gomoku::parseCoordinates("16", "9", &x, &y));
  CHECK(!gomoku::parseCoordinates("a", "9", &x, &y));
  CHECK(!gomoku::parseCoordinates("", "9", &x, &y));
}

}  // namespace

int main() {
  std::printf("===== Gomoku board / protocol tests =====\n");
  TestBasics();
  TestHorizontalWin();
  TestVerticalWin();
  TestDiagonalWins();
  TestLongLine();
  TestDrawOnFullBoard();
  TestFinishAndReset();
  TestBoardString();
  TestProtocolHelpers();

  std::printf("===== %d checks, %d failures =====\n", g_checks, g_failures);
  if (g_failures == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("FAILED\n");
  return 1;
}
