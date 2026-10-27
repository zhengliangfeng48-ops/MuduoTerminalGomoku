# 终端五子棋 CS 系统（基于 muduo 网络库）

纯终端文字界面的五子棋联机对弈系统：服务端基于陈硕 [muduo](https://github.com/chenshuo/muduo) 的
Reactor 网络库，客户端是同仓库的终端程序；账号与战绩（场次 / 胜率）持久化到 MySQL；
附 **webbench** 压力测试与实测数据。

仓库同时是一个 **muduo 学习工程**：`forDsh/` 是已完成并验证的参考实现（冻结，不改），
[`forMe/`](forMe/README.md) 是照着它自己重写 muduo、再测性能、再做优化的工区。

## 学习顺序

**当前进度 14/82**

> 路径相对 `forDsh/muduo/muduo/`；`forMe/muduo/muduo/` 下的同名路径就是你要写的文件。

1. 批 1（1–4）：copyable.h、noncopyable.h、Types.h、WeakCallback.h
2. 批 2（5–12）：StringPiece.h、Atomic.h、Timestamp.h、Timestamp.cc、Date.h、Date.cc、TimeZone.h、TimeZone.cc
3. 批 3（13–19）：CurrentThread.h、CurrentThread.cc、Mutex.h、Condition.h、Condition.cc、CountDownLatch.h、CountDownLatch.cc
4. 批 4（20–23）：LogStream.h、LogStream.cc、Logging.h、Logging.cc
5. 批 5（24–27）：Thread.h、Thread.cc、Exception.h、Exception.cc
6. 批 6（28–37）：FileUtil.h、FileUtil.cc、ProcessInfo.h、ProcessInfo.cc、LogFile.h、LogFile.cc、BlockingQueue.h、BoundedBlockingQueue.h、ThreadPool.h、ThreadPool.cc
7. 批 7（38–42）：AsyncLogging.h、AsyncLogging.cc、ThreadLocal.h、ThreadLocalSingleton.h、Singleton.h
8. 批 8（43–47）：Endian.h、Callbacks.h、TimerId.h、SocketsOps.h、SocketsOps.cc
9. 批 9（48–51）：InetAddress.h、InetAddress.cc、Socket.h、Socket.cc
10. 批 10（52–53）：Buffer.h、Buffer.cc
11. 批 11（54–64）：Channel.h、Channel.cc、Poller.h、Poller.cc、poller/EPollPoller.h、poller/EPollPoller.cc、poller/PollPoller.h、poller/PollPoller.cc、poller/DefaultPoller.cc、EventLoop.h、EventLoop.cc
12. 批 12（65–68）：Timer.h、Timer.cc、TimerQueue.h、TimerQueue.cc
13. 批 13（69–74）：EventLoopThread.h、EventLoopThread.cc、EventLoopThreadPool.h、EventLoopThreadPool.cc、Acceptor.h、Acceptor.cc
14. 批 14（75–82）：TcpConnection.h、TcpConnection.cc、TcpServer.h、TcpServer.cc、Connector.h、Connector.cc、TcpClient.h、TcpClient.cc

**逐批验证**：`build` 是相对路径，必须在"配置过它的那个目录"里执行 —— 也就是 `forMe/muduo/`。
批 1 配置一次，之后每批写完只编译这一个目录里的 muduo（只编 `muduo_base` / `muduo_net` 两个静态库）：

```bash
cd forMe/muduo
cmake -S . -B build              # 只在批 1 配置一次（此后不用再 configure）
cmake --build build -j$(nproc)   # 以后每批写完跑这一条
```

14 批齐了回 `forMe/`：`./scripts/progress.sh` 核对 82/82，再 `./scripts/build.sh` 编整个工程
（含 `Gomoku/`，产物在 `forMe/build/`）。

## 两套实现

| 部分 | 状态 |
|---|---|
| [`forDsh/`](forDsh/README.md) | ✅ 参考实现：单测 591 项、e2e 38 项、webbench 6 档 0 失败、2 万在线 |
| [forMe/](forMe/README.md) | 🚧 muduo 手写 12/82；Gomoku、脚本、压测已复制 |

两套实现共用同一个 MySQL 库（`gomoku`），性能可直接对照。

## 目录

```
├── forDsh/                 ① 参考实现（冻结）：muduo 副本 + Gomoku + 脚本 + 压测 + 结果
├── forMe/                  ② 手写 muduo 的工区：muduo 自己写，其余从 forDsh 复制
├── AGENTS.md               规则 / 学习路线 / 性能口径 / 优化池（新会话必读）
└── README.md               本文件
```

各自目录的 README 只讲自己那一块：`forDsh/README.md`（功能/协议/测试）、
`muduo/README.md`（副本来源与调用链）、`Gomoku/README.md`（组件与状态机）、
`benchmarks/README.md`（压测方法）。

## 快速开始（Linux）

```bash
cd forDsh
./scripts/build.sh                  # muduo + 服务端 + 客户端 + 单测
./build/bin/gomoku_board_test       # 591 项断言，末行 PASS
python3 Gomoku/tests/e2e_test.py    # 38 checks / 0 failures
./scripts/run_server.sh             # 游戏 9527 / HTTP 8080
./scripts/run_client.sh --nick alice --password alice123 --register
```

依赖：`build-essential cmake libboost-dev libmysqlclient-dev mysql-server python3`；
数据库首次初始化 `./scripts/init_db.sh`（库 `gomoku`，账号 `gomoku / gomoku123`）。

## 性能摘要

参考实现实测（i5-13500H / 16 核 / 7810 MB / Ubuntu 22.04，`--io-threads 4`，每档 10 秒）：

| 指标 | 结果 |
|---|---|
| webbench（HTTP 8080，c=1/10/50/100/200/500） | 6 档 **0 失败**；峰值 **35128 req/s**（c=10），之后稳定约 3 万 |
| 游戏协议压测 | c=10 → 4916 msg/s；c=50 → 1895；c=200 → 641；**0 错误** |
| 最大在线客户端 | **20000** 个已认证连接（`/stats` 的 `online=20000`），未触顶 |
| PING 往返 p50 | c=1 → 0.047 ms；c=100 → 1.368 ms；c=500 → 9.650 ms；c=1000 → 21.541 ms |
| CHAT 广播 p50 / p99 | c=50 → 0.446 / 21.0 ms；c=200 → 16.149 / 81.6 ms |

结论：连接能力不是瓶颈（约束是 `ulimit -n`）；**认证吞吐约 210–250 次/秒**（单条 MySQL 连接 +
每事务 fsync）才是"上人慢"的原因；HTTP QPS 平台化是"单 IO 线程 + 每请求短连接"的设计使然。

方法与原始数据见 [forDsh/benchmarks](forDsh/benchmarks/README.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [`AGENTS.md`](AGENTS.md) | 硬规则、学习路线、性能口径、优化池、坑 |
| [`forDsh/README.md`](forDsh/README.md) | 功能、架构、环境、DB、编译运行、客户端、协议、测试、FAQ |
| [`forDsh/muduo/README.md`](forDsh/muduo/README.md) | muduo 副本的来源/包含/排除、构建差异、调用链速查 |
| [`forDsh/Gomoku/README.md`](forDsh/Gomoku/README.md) | 组件职责、锁边界、房间状态机 |
| [`forDsh/benchmarks/README.md`](forDsh/benchmarks/README.md) | 三套压测的方法、参数与解读 |
| [forMe/README.md](forMe/README.md) | 自研工程与 forDsh 的差异、build-ref 对照构建 |
| [forMe/benchmarks/RESULTS.md](forMe/benchmarks/RESULTS.md) | 优化前后对比台账 |

## 许可

- `forDsh/muduo/` 与 `forMe/muduo/`：**BSD 3-Clause**（见各自目录 `License`），著作权归 Shuo Chen。
- `tools/webbench/`：**GPL v3**，原作者 Radim Kolar。
- 本项目的 Gomoku、脚本与文档用于学习与演示。
