# 自研 muduo + Gomoku（学习工程）

这个目录是**照着 `../forDsh` 自己实现 muduo** 的地方：

- `muduo/` —— **要你亲手写**。结构与参考实现一一对应，头文件路径必须完全一致
  （`#include "muduo/net/TcpServer.h"`），否则 `Gomoku/` 编译不过。
- `Gomoku/` —— **原样复制**的网络五子棋服务端 + 终端客户端 + 单测 + e2e，
  一行没改。它是你检验"自己写的 muduo 行不行"的现成负载。
- `scripts/`、`benchmarks/`、`tools/webbench/` —— 原样复制的构建/压测工具，
  只加了一个 `TAG` 参数让结果文件能区分优化轮次。

## 与参考实现对照

| 项 | `../forDsh` | 本目录 |
|---|---|---|
| 角色 | 已完成、已验证的参考实现 | 你的学习实现 |
| muduo 来源 | 上游逐字副本，不改 | 自己写 |
| Gomoku | 同一份代码 | 复制过来，保持不变 |
| 构建产物 | `build/bin/gomoku_server` 等 | 一样 |
| 数据库 | `gomoku`（gomoku/gomoku123） | **同一个库**（便于直接比性能） |
| 软链 | `/forDsh` | `/gomoku-self`（可选，别抢 `/forDsh`） |
| cmake | ≥ 3.10 | ≥ 3.12（muduo 子目录用 `CONFIGURE_DEPENDS` 增量收集源文件） |

## 怎么编译

```bash
# 1) 正常构建（muduo 还没写完时，Gomoku 会因为缺头文件编译失败——这是预期的）
./scripts/build.sh

# 2) 先用参考实现的 muduo 把业务跑通（muduo 一个字没写也能验证 e2e / 压测）
cmake -S . -B build-ref -DUSE_REFERENCE_MUDUO=ON -DMUDUO_REF_DIR=/forDsh/muduo
cmake --build build-ref -j$(nproc)
./build-ref/bin/gomoku_board_test
python3 Gomoku/tests/e2e_test.py --server build-ref/bin/gomoku_server

# 3) 看自己写到哪了
./scripts/progress.sh
```

`muduo/base`、`muduo/net` 的 CMake 用 GLOB 收集 `.cc`：**写完一个文件就能编译一次**，
不需要回来改文件列表；一个 `.cc` 都没有时生成 INTERFACE 占位库，保证 configure 不会失败。

## 学习与优化怎么走

规则、阶段顺序、性能口径、优化记录方法都在仓库根的 **`AGENTS.md`**（新会话必读）。
本目录相关的两份：

- `muduo/README.md` —— 82 个文件的清单（写一个划一个）与增量编译说明
- `benchmarks/RESULTS.md` —— 优化前后对比台账（每轮一行）

## 与参考实现不同的地方（仅这些，都是有意的）

1. 顶层 `CMakeLists.txt`：工程名改 `gomoku_improved`；新增 `USE_REFERENCE_MUDUO` / `MUDUO_REF_DIR`。
2. `muduo/muduo/{base,net}/CMakeLists.txt`：显式文件列表 → GLOB + 空目录守卫；编译参数不变。
3. `scripts/setup_env.sh`：软链名参数化（默认 `/gomoku-self`），**不再抢占 `/forDsh`**。
4. `benchmarks/run_webbench.sh`：新增 `TAG`，结果文件名与 Markdown 头部会带上轮次标签。

需要和参考实现并行跑时，本目录用 `--port 9627 --http-port 8180`，避免端口冲突；
做 A/B 性能对比时则**用同一端口先后跑**，不要并行。

## 许可证

`muduo/License` 保留上游 BSD 3-Clause 声明：接口与结构来自 chenshuo/muduo，
著作权归 Shuo Chen。`tools/webbench/` 遵循 GPL v3。
