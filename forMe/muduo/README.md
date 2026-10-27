# muduo（自研实现）

照着 `../../forDsh/muduo` 自己写的一份 muduo：接口、目录、头文件路径与参考实现一一对应
（`Gomoku/` 的 `#include "muduo/net/TcpServer.h"` 依赖这一点），内部实现自己完成。
许可证与版权见 `License`（BSD 3-Clause，著作权归 Shuo Chen）。

## 规模

| 目录 | 文件 | `.h` | `.cc` |
|---|---:|---:|---:|
| `muduo/base/` | 42 | 27 | 15 |
| `muduo/net/`（含 `poller/`） | 40 | 21 | 19 |
| **合计** | **82** | **48** | **34** |

**写哪个文件、按什么顺序**：见仓库根 [README.md](../../README.md)「学习顺序」，顶部有当前进度。
`./scripts/progress.sh` 给出已实现 N/82、缺失清单，以及自己写的文件与参考实现"逐字相同"的数量。

## 增量编译

`base/`、`net/` 的 CMake 用 `GLOB ... CONFIGURE_DEPENDS` 收集 `.cc`：**写完一个文件就能编译一次**，
新文件在 `cmake --build` 时自动纳入，不必改 CMakeLists。一个 `.cc` 都没有时生成 INTERFACE 占位库，
所以 configure 不会失败；Gomoku 在缺头文件时报错是**预期的进度提示**。

```bash
cd muduo && cmake -S . -B build && cmake --build build -j$(nproc)   # 只编 muduo -> muduo/build/
cd .. && ./scripts/build.sh                                         # 编整个工程 -> build/
```

`muduo/build/`（只有两个静态库）与顶层 `build/`（整个工程）是两棵独立构建树，别混用二进制。
`build` 是相对路径，`cmake --build build` 只认当前目录里配置过的那个构建树。
单独 configure 时库在 `muduo/build/muduo/{base,net}/` 下；作为顶层子目录构建时统一到 `build/lib/`。

编译参数与参考实现一致（见 `CMakeLists.txt` 顶部注释），产物只有 `muduo_base`、`muduo_net` 两个静态库。

## 与参考实现不同的写法

1. **`cacheTid / isMainThread / sleepUsec` 放进 `CurrentThread.cc`。**
   参考实现把它们定义在 `Thread.cc`，于是只编 `Logging.cc` + `CurrentThread.cc` 会 undefined reference；
   放一起后依赖是单向的。
2. **参考实现有两处悬空声明**，照抄会链接失败，用到时自己补：
   `StringPiece.h` 声明了 `operator<<(std::ostream&, const StringPiece&)` 却没有 `StringPiece.cc`；
   `TimeZone.h` 声明了 `static TimeZone China()` 却没有实现。Gomoku 没用到它们。

## 验收

```bash
./scripts/progress.sh                     # 82/82，逐字相同数应接近 0
cd .. && ./scripts/build.sh               # 整个工程
./build/bin/gomoku_board_test             # PASS（591 项）
python3 Gomoku/tests/e2e_test.py          # 38 checks, 0 failures
```

通过后进入性能基线与优化，见 [../benchmarks/RESULTS.md](../benchmarks/RESULTS.md)。
