# AGENTS.md —— 这个工作区是干什么的（所有新会话先读这里）

> **这份文件是唯一入口。** 原来放在 `.agent/HANDOFF.md` 的交接信息已经并入本文（该目录已删除），
> 新会话不要再去别处找交接文档。

---

## 1. 一句话现状

仓库里有两套终端的五子棋 CS 系统：

- **`forDsh/`** —— 已完成并验证过的**参考实现**（muduo 逐字副本 + Gomoku + MySQL 战绩 + webbench 压测）。
- **`基于改进muduo高性能网络库的终端五子棋对弈CS模型/`**（下称**自研目录**）—— 用户**自己重写 muduo** 的地方。
  骨架已搭好、Gomoku 已复制、参考 muduo 的构建路径已跑通；**用户手写的 muduo 目前是 0/82 个文件**。

---

## 2. 用户最终要做什么

1. **学习 muduo 源码**：跟着 `forDsh/muduo` 在自研目录里**自己写**一份等价的 muduo；
   除 `muduo/` 以外的部分（Gomoku、脚本、压测工具）直接从 `forDsh` 复制，不再重写。
2. **测自己的代码**：QPS、最大在线客户端数、客户端往返延迟。
3. **改进与优化 muduo**：基于自己的理解或 AI 的建议做优化，**每轮优化后重测，与优化前对比**。

所以"把功能跑通"只是及格线，**性能前后对比**才是这个仓库的第二条主线。

---

## 3. 硬规则（违反就是帮倒忙）

| # | 规则 |
|---|---|
| **R1** | `forDsh/` 是**冻结的参考实现**：不改、不格式化、不"顺手优化"，尤其 `forDsh/muduo` 一个字都不动。 |
| **R2** | **自研目录里的 muduo 必须用户自己写。** AI 的角色是：讲原理、画调用链、给接口签名与伪代码、给测试思路、review 与纠错、诊断编译/运行错误。**不要直接输出完整的 muduo 实现**（用户明确说"直接给我实现"时才例外）；**更不许**把 `forDsh/muduo` 的文件复制或"补全"到自研目录。 |
| **R3** | 自研目录里非 muduo 的代码是"最小改动复制"来的：改协议、数据库、端口、脚本语义之前先说清理由。 |
| **R4** | 优化必须可回退：**一次只动一个假设**，改之前先用同一命令复测基线，结果落盘可复现。 |
| **R5** | 性能数字必须同口径（见 §9）：不同口径的数字不许放在同一张表里比较。 |
| **R6** | 保持用户的代码风格：中文注释、muduo 原有的类名/文件名/头文件路径不变（否则 `Gomoku/` 的 `#include "muduo/net/TcpServer.h"` 会断）。 |
| **R7** | 提交前跑"验证三件套"：`gomoku_board_test`（591 项）、`Gomoku/tests/e2e_test.py`（38 项）、客户端冒烟。 |
| **R8** | 不改历史基准文件 `benchmarks/results/baseline-forDsh-*`；新结果另存。 |
| **R9** | 不允许改 `/forDsh` 软链指向（`forDsh/scripts/setup_env.sh` 会抢它，自研目录的 `setup_env.sh` 已改成建 `/gomoku-self`）。 |
| **R10** | 编译失败先按三类高频原因查：include 根不对、缺 `-DNO_ACCEPT4`、缺 `CHECK_PTHREAD_RETURN_VALUE` / `_FILE_OFFSET_BITS=64`。 |

除此之外：**密码明文存储是需求**（别当 bug 修）；**muduo 上游代码不要"优化"**（参考实现被要求保持原样）。

---

## 4. 目录与角色

```
仓库根/
├── AGENTS.md                  ← 本文（新会话唯一入口）
├── README.md                  ← 项目总览（给人看）
├── .gitattributes             ← 统一 LF，防止重新 clone 后脚本报 bad interpreter
├── forDsh/                    ← 参考实现（冻结）
│   ├── README.md              ← 目录/DB/编译/协议/测试/压测/FAQ（825 行，最全）
│   ├── muduo/                 ← 上游逐字副本（82 个源文件）+ 3 份 CMake
│   ├── Gomoku/                ← 服务端/客户端/协议/单测/e2e + README（组件设计说明）
│   ├── scripts/               ← setup_env / build / init_db / run_server / run_client
│   ├── benchmarks/            ← webbench 矩阵、协议压测、规模与延迟压测 + results/
│   └── tools/webbench/        ← 内置 webbench 1.5（一处可移植性补丁）
└── 基于改进muduo高性能网络库的终端五子棋对弈CS模型/   ← 自研目录
    ├── README.md              ← 自研工程说明（与 forDsh 的差异都在这）
    ├── CMakeLists.txt         ← 多一个 USE_REFERENCE_MUDUO 开关
    ├── muduo/                 ← 要手写：README.md 里有 82 文件清单与顺序
    ├── Gomoku/                ← 从 forDsh 原样复制（33 文件）
    ├── scripts/               ← +progress.sh；setup_env.sh 改成不抢 /forDsh
    ├── benchmarks/            ← +RESULTS.md（前后对比台账）；run_webbench.sh 支持 TAG
    └── tools/webbench/        ← 原样复制
```

---

## 5. 学习路线（照着写，一段一段验收）

> 82 个文件的逐行清单在 [`自研目录/muduo/README.md`](基于改进muduo高性能网络库的终端五子棋对弈CS模型/muduo/README.md)，
> 进度用 `./scripts/progress.sh` 查。

### 阶段 A：`muduo_base`（15 个 .cc + 27 个头）

按依赖从下往上，每写完一批就能编译一次：

1. `copyable / noncopyable / Types`
2. `StringPiece / Atomic / Timestamp / Date / TimeZone`
3. `CurrentThread → Mutex → Condition → CountDownLatch`
   （把 `cacheTid/isMainThread/sleepUsec` 写进 `CurrentThread.cc`，否则会和 `Thread.cc` 成环）
4. `LogStream → Logging`（能打一行带时间戳、tid、级别、文件行的日志）
5. `Exception / Thread`（`Thread::start()` 用 latch 同步 tid）
6. `FileUtil / ProcessInfo / LogFile / BlockingQueue / BoundedBlockingQueue / AsyncLogging`
7. `ThreadPool / ThreadLocal / ThreadLocalSingleton / Singleton / WeakCallback`

**验收**：`cd muduo && cmake -S . -B build && cmake --build build` 全绿。

### 阶段 B：`muduo_net`（19 个 .cc + 21 个头）

1. `Endian / SocketsOps / InetAddress / Socket`
2. `Buffer`（`readv` + 栈上 64K 缓冲）
3. `Channel`（`tie_` / `index_` / `handleEventWithGuard` 的判序）
4. `Poller → EPollPoller → PollPoller → DefaultPoller`
5. `EventLoop`（eventfd 唤醒、`pendingFunctors_` swap、`runInLoop/queueInLoop`）
6. `Timer / TimerId / TimerQueue`（单 timerfd + 有序 set）
7. `EventLoopThread / EventLoopThreadPool`
8. `Acceptor`（`idleFd_` 处理 EMFILE）
9. `TcpConnection`（四态、send 直写、`outputBuffer_`、水位回调）
10. `TcpServer`、`Connector / TcpClient`

**验收**：`cd .. && ./scripts/build.sh` 全绿，`./build/bin/gomoku_board_test` 输出 `PASS`。

### 阶段 C：跑通业务 + 量出基线

```bash
python3 Gomoku/tests/e2e_test.py        # 期望 38 checks, 0 failures
TAG=self-base ./benchmarks/run_webbench.sh
python3 benchmarks/scale_test.py max-online --levels 1000 4000
TAG=self-base python3 benchmarks/scale_test.py latency -c 100 -t 10
```

**验收**：三项指标都有数字，并写进 `自研目录/benchmarks/RESULTS.md`（"轮次 0 / self-base"）。

### 阶段 D：优化循环

一次一轮：**选一个假设 → 改 → 同口径复测 → 填 RESULTS.md → 决定保留还是回滚**。
候选池见 §11。判断标准不是"看起来更快"，而是数字 + p99 + 失败数为 0。

---

## 6. 关键设计（改代码前必须知道）

1. **同进程两个 TcpServer**：9527 游戏协议、8080 极简 HTTP（`/`、`/health`、`/stats`）。
   `TcpServer::start()` 必须**投递到它自己的 loop 线程**执行（内部 `EventLoopThreadPool::start()`
   会断言"当前是 base loop 线程"），`ServerApp::start()` 用 `CountDownLatch + runInLoop` 做到这点。
2. **锁边界**：`Session` 的 `authed_/nickname_/roomId_` 只属于本连接的 IO 线程，不加锁；
   `RoomManager`/`Room` 一把全局 `std::mutex`；`AuthService`/`Db` 各自一把锁（`MYSQL*` 非线程安全）；
   在线表与统计用 mutex + atomic。广播走 `TcpConnection::send()`，muduo 内部负责跨线程排队。
3. **Session 只持连接的 `weak_ptr`**：连接通过 `setContext()` 持有 Session 的 `shared_ptr`，
   反向强持有就成环泄漏。
4. **先广播 `OVER`、后写数据库**：结算在房间操作里产生 `GameOverInfo`，交给 `ServerApp`
   在**释放房间锁之后**写库并推 `INFO`。代价是"收到 OVER 时库可能还没写完"，
   所以 e2e 用**轮询**校验战绩（`wait_stats`）。
5. **`GomokuGame` 构造出来是"未开局"**：构造只清棋盘（`running_=false`），
   `Room::startGame()` 调 `reset()` 才真正开局，否则房间刚建好就不能上座。
6. **注册流程顺序**：先 `AuthService::registerUser`（撞重名 → `NAME_TAKEN`），成功后再抢在线名额；
   反过来会把重复注册报成 `ALREADY_ONLINE`。
7. **MySQL 两个坑（别改回去）**：
   - 整数除法：必须写 `games + 1.0`；
   - `SET` 从左到右求值：`win_rate` 必须写在 `games` / `wins` **之前**，
     否则用的是更新后的值（1/2 会被算成 2/3）。
8. **协议约定**：UTF-8 文本行、`\n` 分帧、单行 ≤1024 字节、聊天 ≤512 字节；
   昵称 `[A-Za-z0-9_]` 2–16 位且首字符字母数字；房号 `[0-9A-Za-z]` 1–16 位；
   15×15 棋盘、黑先、任意方向 ≥5 连即胜（无禁手）、满盘和棋；
   对局中第三人 `SIT` → `GAME_RUNNING`、观战者 `CHAT` → `CHAT_LOCKED`；
   起身/断线 = 认输（`FORFEIT_STAND` / `FORFEIT_LEFT`）；单房间上限 64 人。

---

## 7. 环境与端口约定

| 项 | 值 |
|---|---|
| 系统 | Windows + WSL2 Ubuntu 22.04（systemd 为 PID1），**只能在 WSL 里编译运行** |
| 机器 | i5-13500H / 16 核 / 7810 MB / 内核 6.6.114 |
| 工具链 | g++ 11.4.0、CMake 3.22.1、Python 3、MySQL 8.0.46 |
| 依赖 | build-essential、cmake、libboost-dev、libmysqlclient-dev、mysql-server、python3 |
| 数据库 | WSL 内 MySQL：库 `gomoku`，账号 `gomoku / gomoku123`（**两套实现共用**） |
| 软链 | `/forDsh` → 参考实现（不动）；`/gomoku-self` → 自研目录（可选） |
| 端口 | 参考实现 9527/8080；自研版并行时用 9627/8180；**做 A/B 时同端口先后跑，不并行** |

```bash
# 在 Windows 侧调用 WSL（路径含中文，注意引号）
wsl -d Ubuntu-22.04 -- bash -lc 'cd "/mnt/d/coding/Terminal-Gomoku-game-CS-model-based-on-improved-muduo-high-performance-network-library/基于改进muduo高性能网络库的终端五子棋对弈CS模型" && ./scripts/build.sh'
```

数据库初始化（需要时）：`cd forDsh && ./scripts/init_db.sh`（建库建表建账号；两套实现共用同一个库）。

---

## 8. 命令速查（都在自研目录里执行）

```bash
# 编译
./scripts/build.sh                      # -> build/（自研 muduo）
cmake -S . -B build-ref -DUSE_REFERENCE_MUDUO=ON -DMUDUO_REF_DIR=/forDsh/muduo
cmake --build build-ref -j$(nproc)      # -> build-ref/（参考 muduo，用来对照/救急）

# 测试
./build/bin/gomoku_board_test                        # 期望 PASS（591 项）
python3 Gomoku/tests/e2e_test.py                     # 期望 38 checks, 0 failures
./scripts/progress.sh                                # muduo 进度 N/82

# 跑服务
./scripts/run_server.sh                              # 9527 / 8080
./scripts/run_client.sh --nick alice --password alice123 --register
curl -s http://127.0.0.1:8080/health                 # {"status":"ok"}
curl -s http://127.0.0.1:8080/stats                  # 运行统计 JSON

# 三项性能指标
TAG=<轮次> ./benchmarks/run_webbench.sh                                # QPS（结果自带 tag）
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000   # 最大在线
python3 benchmarks/scale_test.py latency -c 100 -t 10 \
  | tee benchmarks/results/scale-latency-<轮次>-$(date +%Y%m%d-%H%M%S).md   # 延迟(PING)
python3 benchmarks/scale_test.py latency -c 50 -t 10 --payload chat --rooms 4 \
  | tee benchmarks/results/scale-chat-<轮次>-$(date +%Y%m%d-%H%M%S).md      # 延迟(CHAT)
```

`scale_test.py` 只把报告打印到 stdout、不写文件，所以要用 `tee` 留档（文件名带轮次）。

有用的覆盖参数：`run_webbench.sh` 认 `SERVER_BIN`（测 `build-ref` 的二进制时用）、
`TAG`（结果文件名带轮次）、`NO_START=1`（复用已启动的服务端）、`LEVELS` / `DURATION`。

---

## 9. 性能口径与参考基线

**固定口径**：同一台机器、`--io-threads 4`、每档 10 秒、`ulimit -n 65535`、
日志写 `/tmp`（9p 盘会拖慢 AsyncLogging）、压测期间不跑别的负载、A/B 同端口先后跑。

参考实现实测（`forDsh/benchmarks/results/`，可作对比锚点，已复制到
`自研目录/benchmarks/results/baseline-forDsh-*`）：

| 指标 | 结果 |
|---|---|
| webbench（HTTP 8080，c=1/10/50/100/200/500） | **0 失败**；峰值 **35128 req/s**（c=10），之后稳定约 3 万 |
| 游戏协议压测 gomoku_bench | c=10 → 4916 msg/s；c=50 → 1895；c=200 → 641；**0 错误** |
| 最大在线客户端 | **20000** 个已认证连接（`/stats` 的 `online=20000`），未触顶 |
| PING 往返 p50 | c=1 → 0.047 ms；c=100 → 1.368 ms；c=500 → 9.650 ms；c=1000 → 21.541 ms |
| CHAT 广播 p50 / p99 | c=50 → 0.446 / 21.0 ms；c=200 → 16.149 / 81.6 ms |

解读要点：

- **认证吞吐约 210–250 次/秒**才是"涨到两万在线要一分多钟"的原因（单条 MySQL 连接 + 每事务 fsync），
  不是网络层；想提升它要改 `Db`（应用层）。
- **HTTP 端点 QPS 平台化**是设计使然：单 IO 线程 + 每请求短连接。
- 延迟看 **p99**，别看 max（首轮建连/调度抖动会拉出离群点）。
- 结论只在**同环境**内做相对比较。

---

## 10. 验证状态与需求对照

本次会话在自研目录、用**参考 muduo**复跑过（证明复制过来的业务代码与原版等价）：

| 项目 | 结果 |
|---|---|
| `gomoku_board_test` | **591 项断言，0 失败，PASS** |
| `Gomoku/tests/e2e_test.py` | **38 checks, 0 failures, PASS** |
| `./scripts/build.sh`（空 muduo） | configure 成功；build 停在 `muduo/net/Buffer.h: No such file`（或 `muduo/base/Logging.h`）—— **预期**的进度提示 |
| webbench 冒烟（`TAG=smoke`，c=10，2s） | 0 失败，QPS 31452，结果文件带 tag 与二进制指纹 |

需求（原始作业）对照：

| 需求 | 实现位置 |
|---|---|
| muduo 核心代码放 `/forDsh/muduo`，不做改进 | `forDsh/muduo/`（82 文件 + 3 份 CMake） |
| 用该库封装终端五子棋服务端与客户端 | `Gomoku/server/`、`Gomoku/client/` |
| 输入房间号进入（无则创建） | `RoomManager::join` |
| 房间无人对弈即可上座 | `Room::sit`（两座坐满立即开局） |
| 非对弈期全房聊天、对弈期仅双方可聊 | `Room::chat`（`GAME_RUNNING` / `CHAT_LOCKED`） |
| 启动需注册（昵称 + 密码） | `ServerApp::handleRegister` + `AuthService` |
| 昵称唯一、场次/胜率入库 | `sql/schema.sql`、`AuthService::recordResult`（事务） |
| webbench 压力/性能测试 | `tools/webbench/` + `benchmarks/`（含实测结果） |
| README 说明目录/DB/编译/启动/压测 | `forDsh/README.md`（+ 3 份子 README） |
| 注释写清楚 | 全部 Gomoku 代码中文注释；muduo 保持上游注释 |

**本仓库新增的目标（作业之外）**：在自研目录里手写 muduo、量三项指标、做优化前后对比。

---

## 11. 优化候选池（阶段 D 从这里挑）

> 标注 **[muduo]** 的是网络库本身的改动（属于"改进 muduo"这条线），
> **[应用层]** 的改动不属于 muduo 优化，记录时要说清楚，别混进对比表还当成 muduo 的功劳。

| # | 方向 | 类型 | 当前做法 → 可能方向 | 预期 / 风险 |
|---|---|---|---|---|
| 1 | 批量 accept | [muduo] | `Acceptor::handleRead` 一次只 accept 一个（上游 FIXME）；改 `while(accept4(...) >= 0)` + `EPOLLET` | 突发建连时 syscall 减半；ET 下漏连接风险，需测 `max-online` |
| 2 | 发送路径零拷贝 | [muduo] | 跨线程 `send` 走 `message.as_string()` 整块拷贝；改传 `Buffer`/`string&&` 所有权（`Buffer::swap` 已有） | 大响应体明显；注意生命周期 |
| 3 | output buffer 分段 | [muduo] | 单个连续 `Buffer` 积压时 `makeSpace` 是 O(n) 前移；换链式/环形缓冲（上游 FIXME 同方向） | 慢客户端场景收益大；实现复杂度高 |
| 4 | 水位变真流控 | [muduo] | `highWaterMark_` 只通知不阻断（默认 64MB）；越线后挂起该连接处理 | 防 OOM；会改变业务语义，要确认 |
| 5 | 唤醒合并 | [muduo] | `queueInLoop` 每次可能 `write(eventfd, 8)`；改成"有 pending 才写"标志位/批量投递 | 跨线程小包场景收益；注意丢唤醒 |
| 6 | 定时器结构 | [muduo] | `std::set<pair<Timestamp,Timer*>>` + 第二个 set 做 cancel 索引；换时间轮或最小堆 | 高频短定时收益；游戏里心跳/倒计时可测 |
| 7 | poller 事件数组回收 | [muduo] | `events_` 只翻倍不缩容，历史峰值决定常驻内存；加水位回收/上限 | 内存曲线更平；注意每次 `resize` 成本 |
| 8 | 每事件开销 | [muduo] | 每事件 1 次 `weak_ptr::lock` + `std::function` 间接调用 + `shared_from_this`；`tie_` 只给"可能自析构"的连接开 | 高 PPS 下 CPU 有感；改回调签名影响面大 |
| 9 | 连接分配 | [muduo] | 只有 round-robin / hash；按 loop 负载（队列长度/待写字节）选 | 原项目房间型业务容易倾斜；需先造倾斜场景 |
| 10 | 认证吞吐 | [应用层] | `Db` 单连接 + 每事务 fsync；换连接池或"DB 线程 + 任务队列" | 直接抬高 `max-online` 斜率；不是 muduo 改动 |
| 11 | HTTP 端点 | [应用层] | 单 IO 线程 + 每请求短连接；`setThreadNum(N)` 或 keep-alive | webbench 数字大涨；也不是 muduo 改动 |

---

## 12. 坑、文档地图、AI 回答规范

### 容易踩的坑

1. **`/forDsh` 软链**：跑 `forDsh/scripts/setup_env.sh` 会把 `/forDsh` 指向它自己所在的目录；
   自研目录的 `setup_env.sh` 已改成建 `/gomoku-self`。别手工把 `/forDsh` 改指到自研目录。
2. **空 muduo 时编译失败是预期**：`./scripts/build.sh` 会停在 `muduo/net/Buffer.h: No such file`
   （或 `muduo/base/Logging.h`）。想先跑业务用 `-DUSE_REFERENCE_MUDUO=ON`。
3. **`build/` 与 `build-ref/`** 都被 `.gitignore` 忽略（`build/`、`build-*/`）。别把两者的二进制搞混：
   跑基线/对照用 `build-ref/`，日常用自己的 `build/`。
4. **先广播 OVER 后写库**：查战绩要**轮询**，别收到 OVER 立刻断言。
5. **e2e / 压测会写数据库**：跑完 `users`、`match_history` 就不是空表了（压测账号前缀 `bench_`/`scale_`）。
6. **webbench 三个坑**：URL 必须 `/` 结尾；用默认 HTTP/1.0（别加 `-2`）；`ulimit -n 65535`。
7. **CRLF**：仓库已加 `.gitattributes`（`* text=auto eol=lf`）。若在 Windows 上重新 clone 后
   `./scripts/*.sh` 报 bad interpreter，先查这点。
8. **9p 文件系统慢**：源码在 `/mnt/d` 上，编译比 WSL 原生盘慢；压测时日志写 `/tmp`。
9. **密码明文**：需求如此，不要"修"。
10. **端口占用**：9527/8080 可能被上一次没退干净的服务端占着，先 `ss -lntp | grep 9527`。

### 文档地图

| 文档 | 作用 |
|---|---|
| `README.md`（根） | 项目总览：两套实现、怎么跑、性能摘要 |
| `AGENTS.md`（本文） | 规则、路线、口径、坑 —— **新会话必读** |
| `基于改进…/README.md` | 自研工程与 forDsh 的全部差异 |
| `基于改进…/muduo/README.md` | **82 文件清单** + 依赖顺序 + 增量编译 |
| `基于改进…/benchmarks/RESULTS.md` | **优化前后对比台账**（每轮一行） |
| `forDsh/README.md` | 参考实现最全的说明（目录/DB/协议/测试/压测/FAQ） |
| `forDsh/muduo/README.md` | 上游副本的来源、包含/排除、调用链速查 |
| `forDsh/Gomoku/README.md` | 组件职责、线程模型、锁边界、状态机 |
| `forDsh/benchmarks/README.md` | 三套压测的方法与参数 |

### AI 回答规范

1. **先给结论**（能做/不能做/为什么），再给原理与调用链，最后给"你要写的接口或伪代码"。
2. **绝不整段贴 muduo 实现**；需要代码时给**签名 + 注释 + 分步提示**，让用户自己写。
3. 纠错时指出**具体文件与行为**（"`Channel::handleEvent` 少了 `tie_.lock()`，连接在回调里被析构就 UAF"），
   不要只说"这样不好"。
4. 谈性能先对齐口径（§9），没有数字就别说"更快"。
5. 不确定就去读源码再回答；不要凭印象编 muduo 的 API。
