# 自研 muduo + Gomoku（学习工区）

照着 `../forDsh` **自己重写一份 muduo** 的地方：

- `muduo/` —— **自己写**。目录结构与头文件路径必须与参考实现一致
  （`#include "muduo/net/TcpServer.h"`），否则 `Gomoku/` 编译不过。
- `Gomoku/`、`scripts/`、`benchmarks/`、`tools/` —— 从 `forDsh` **原样复制**（业务代码不改），
  它们是检验"自己写的 muduo 行不行"的现成负载。

## 与参考实现的差异

| 项 | `../forDsh` | 本目录 |
|---|---|---|
| 角色 | 已完成、已验证的参考实现 | 学习实现 |
| muduo | 上游逐字副本 | 自己写 |
| 构建产物 | `build/bin/*` | 一样 |
| 数据库 | `gomoku`（gomoku/gomoku123） | **同一个库**（便于直接比性能） |
| 软链 | `/forDsh` | `/gomoku-self`（可选） |
| cmake | ≥ 3.10 | ≥ 3.12（用 `CONFIGURE_DEPENDS` 增量收集源文件） |

有意的改动只有四处：顶层 `CMakeLists.txt` 加 `USE_REFERENCE_MUDUO` / `MUDUO_REF_DIR` 开关；
`muduo/muduo/{base,net}/CMakeLists.txt` 显式文件列表 → GLOB + 空目录守卫（编译参数不变）；
`scripts/setup_env.sh` 建 `/gomoku-self` 而不是抢 `/forDsh`；`benchmarks/run_webbench.sh` 支持 `TAG`。

## 编译

```bash
./scripts/build.sh                 # 整个工程 -> build/；没写完时 Gomoku 缺头文件报错是预期的
./scripts/progress.sh              # 进度 N/82、缺失清单、与参考实现逐字相同的文件数

# 只编 muduo（逐批学习时每批跑一次；产物是 libmuduo_base.a / libmuduo_net.a）
cmake -S muduo -B muduo/build && cmake --build muduo/build -j$(nproc)   # -> muduo/build/

# 用参考实现的 muduo 编一份，验证业务代码 + 做 A/B 对照
cmake -S . -B build-ref -DUSE_REFERENCE_MUDUO=ON -DMUDUO_REF_DIR=/forDsh/muduo
cmake --build build-ref -j$(nproc)
python3 Gomoku/tests/e2e_test.py --server build-ref/bin/gomoku_server
```

三个构建目录别混用、也别混用二进制：`build/` 是整个工程（自己的 muduo，产物在 `build/bin` + `build/lib`），
`muduo/build/` 只有 muduo 两个静态库（在 `muduo/build/muduo/{base,net}/`），`build-ref/` 是链接参考 muduo 的对照。

## CMakeLists 分层

四份 `CMakeLists.txt` 是 CMake 的目录作用域（`add_subdirectory` 逐层展开），不是重复配置：

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层 `project(gomoku_improved)`：C++11/Release、产物集中到 `build/bin` + `build/lib`、`USE_REFERENCE_MUDUO` 开关；`add_subdirectory(muduo)` 与 `add_subdirectory(Gomoku)` |
| `muduo/CMakeLists.txt` | muduo 子工程，**可单独 configure**：定 include 根（就是本目录）、编译选项/宏、Threads/rt，再 `add_subdirectory(muduo/base)`、`add_subdirectory(muduo/net)` |
| `muduo/muduo/base/CMakeLists.txt` | 目标 `muduo_base`：GLOB 收集 `*.cc`，目录为空时生成 INTERFACE 占位库 |
| `muduo/muduo/net/CMakeLists.txt` | 目标 `muduo_net`：GLOB 收集 `*.cc` + `poller/*.cc`，链接 `muduo_base`，探测 `accept4` |

`muduo/muduo/` 这层重名不是笔误：头文件按 `#include "muduo/base/Logging.h"` 引用，
所以 `forMe/muduo/` 是 include 根，里面必须再有一个包目录 `muduo/` 放 `base/`、`net/`。

## 学习顺序与进度

见仓库根 [README.md](../README.md)「学习顺序」（14 批、82 个文件，顶部写着当前进度）。
规则、性能口径、优化记录方法见 [AGENTS.md](../AGENTS.md)。

并行跑两套服务时用 9627/8180；做 A/B 对比时**同端口先后跑，不并行**。
