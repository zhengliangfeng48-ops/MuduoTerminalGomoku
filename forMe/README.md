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
./scripts/build.sh                 # 自己的 muduo；没写完时 Gomoku 缺头文件报错是预期的
./scripts/progress.sh              # 进度 N/82、缺失清单、与参考实现逐字相同的文件数

# 用参考实现的 muduo 编一份，验证业务代码 + 做 A/B 对照
cmake -S . -B build-ref -DUSE_REFERENCE_MUDUO=ON -DMUDUO_REF_DIR=/forDsh/muduo
cmake --build build-ref -j$(nproc)
python3 Gomoku/tests/e2e_test.py --server build-ref/bin/gomoku_server
```

`build/` 是自己的，`build-ref/` 是对照的，别混用二进制。

## 学习顺序与进度

见仓库根 [README.md](../README.md)「学习顺序」（14 批、82 个文件，顶部写着当前进度）。
规则、性能口径、优化记录方法见 [AGENTS.md](../AGENTS.md)。

并行跑两套服务时用 9627/8180；做 A/B 对比时**同端口先后跑，不并行**。
