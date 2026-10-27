# AGENTS.md —— 工作区说明（新会话先读）

本文是唯一入口，不要到别处找交接文档。文档地图见 §12。

## 1. 现状

- `forDsh/` —— **参考实现**，已完成并验证（muduo 逐字副本 + Gomoku + MySQL 战绩 + webbench 压测）。**冻结**。
- `forMe/`（旧名 `基于改进muduo高性能网络库的终端五子棋对弈CS模型/`）—— 用户自己重写 muduo 的地方。
  骨架、Gomoku、脚本、压测都已就位；**手写 muduo 进度 12/82**（以根 `README.md` 顶部为准，实时用 `./scripts/progress.sh`）。
- 两套实现共用同一个 MySQL 库 `gomoku`，性能可直接对照。

## 2. 目标

1. **学 muduo**：跟着 `forDsh/muduo` 在 `forMe/` 里自己写一份等价实现（只有 `muduo/` 要写）。
2. **测自己的代码**：QPS、最大在线客户端数、客户端往返延迟。
3. **改进并优化 muduo**：每轮优化后同口径复测，与优化前对比。

"功能跑通"只是及格线，**性能前后对比**是第二条主线。

## 3. 硬规则

| # | 规则 |
|---|---|
| **R1** | `forDsh/` 冻结：不改、不格式化、不"顺手优化"，`forDsh/muduo` 一个字都不动。 |
| **R2** | `forMe/` 的 muduo **必须用户自己写**。AI 只做：讲原理、画调用链、给接口签名与伪代码、给测试思路、review 与纠错、诊断编译/运行错误。**不输出完整实现**（用户明确要求时例外），**更不许**把 `forDsh/muduo` 的文件复制或补全过来。 |
| **R3** | `forMe/` 里非 muduo 的代码是"最小改动复制"：改协议、数据库、端口、脚本语义前先说清理由。 |
| **R4** | 优化必须可回退：**一次只动一个假设**，先用同一命令复测基线，结果落盘可复现。 |
| **R5** | 性能数字必须同口径（§9）；不同口径的数字不进同一张表。 |
| **R6** | 保持代码风格：中文注释；muduo 的类名/文件名/头文件路径不变（否则 `Gomoku/` 的 `#include "muduo/net/TcpServer.h"` 会断）。 |
| **R7** | 提交前跑验证三件套：`gomoku_board_test`（591 项）、`Gomoku/tests/e2e_test.py`（38 项）、客户端冒烟。 |
| **R8** | 不改历史基准 `benchmarks/results/baseline-forDsh-*`；新结果另存。 |
| **R9** | 不改 `/forDsh` 软链指向（`forDsh/scripts/setup_env.sh` 会抢它；`forMe/` 建 `/gomoku-self`）。 |
| **R10** | 编译失败先查三类高频原因：include 根不对、缺 `-DNO_ACCEPT4`、缺 `CHECK_PTHREAD_RETURN_VALUE` / `_FILE_OFFSET_BITS=64`。 |

另外：**密码明文存储是需求**，别当 bug 修。

## 4. 目录与角色

```
仓库根/
├── README.md                 项目总览 + 学习顺序（顶部有进度）
├── AGENTS.md                 本文
├── .gitattributes            固定 LF，防止脚本报 bad interpreter
├── forDsh/                   参考实现（冻结）
│   ├── muduo/                上游逐字副本，82 个源文件 + 3 份 CMake
│   ├── Gomoku/               服务端/客户端/协议/单测/e2e
│   ├── scripts/              setup_env / build / init_db / run_server / run_client
│   ├── benchmarks/           webbench 矩阵、协议压测、规模与延迟压测 + results/
│   └── tools/webbench/       内置 webbench 1.5（一处可移植性补丁）
└── forMe/                    自研工区（旧名"基于改进muduo…CS模型/"）
    ├── CMakeLists.txt         顶层工程：C++11/Release、产物布局、USE_REFERENCE_MUDUO 开关
    ├── muduo/                 要手写（base 42 + net 40）
    │   └── CMakeLists.txt     可单独 configure：只编 muduo 两个静态库（逐批验证用）
    ├── Gomoku/               原样复制
    ├── scripts/              + progress.sh
    ├── benchmarks/           + RESULTS.md（前后对比台账），run_webbench.sh 支持 TAG
    └── tools/webbench/       原样复制
```

## 5. 学习路线与验收

82 个文件的**逐文件顺序在根 `README.md`「学习顺序」**（14 批），进度用 `./scripts/progress.sh` 查。
每批写完都要能编译；四段验收：

| 阶段 | 内容 | 验收 |
|---|---|---|
| A | `muduo/base`（42） | `cd forMe/muduo && cmake -S . -B build && cmake --build build` 全绿（`build` 是 `forMe/muduo/build`，只编两个静态库） |
| B | `muduo/net`（40） | `cd forMe && ./scripts/build.sh` 全绿，`gomoku_board_test` PASS |
| C | 跑通业务 + 量基线 | `python3 Gomoku/tests/e2e_test.py` → 38 checks 0 failures；三项指标写进 `benchmarks/RESULTS.md`（轮次 0 / self-base） |
| D | 优化循环 | 选假设 → 改 → 同口径复测 → 填 RESULTS.md → 保留或回滚 |

阶段 D 的候选池见 §11，判断标准是数字 + p99 + 失败数为 0，不是"看起来更快"。

## 6. 关键设计（改代码前必须知道）

1. **同进程两个 TcpServer**：9527 游戏协议、8080 极简 HTTP（`/`、`/health`、`/stats`）。
   `TcpServer::start()` 必须投递到它自己的 loop 线程（`EventLoopThreadPool::start()` 会断言
   "当前是 base loop 线程"），`ServerApp::start()` 用 `CountDownLatch + runInLoop` 做到。
2. **锁边界**：`Session` 的 `authed_/nickname_/roomId_` 只属于本连接 IO 线程，不加锁；
   `RoomManager`/`Room` 一把全局 mutex；`AuthService`/`Db` 各一把（`MYSQL*` 非线程安全）；
   在线表与统计用 mutex + atomic。广播走 `TcpConnection::send()`，muduo 内部负责跨线程排队。
3. **Session 只持连接的 `weak_ptr`**：连接通过 `setContext()` 持有 Session 的 `shared_ptr`，
   反向强持有就成环泄漏。
4. **先广播 `OVER`、后写数据库**：结算在房间操作里产生 `GameOverInfo`，交给 `ServerApp` 在
   **释放房间锁之后**写库并推 `INFO`。代价是"收到 OVER 时库可能还没写完"，所以 e2e 用**轮询**校验战绩。
5. **`GomokuGame` 构造出来是"未开局"**：构造只清棋盘（`running_=false`），`Room::startGame()`
   调 `reset()` 才真正开局，否则房间刚建好就不能上座。
6. **注册流程顺序**：先 `AuthService::registerUser`（撞重名 → `NAME_TAKEN`），成功后再抢在线名额；
   反过来会把重复注册报成 `ALREADY_ONLINE`。
7. **MySQL 两个坑（别改回去）**：整数除法必须写 `games + 1.0`；`SET` 从左到右求值，
   `win_rate` 必须写在 `games`/`wins` 之前（否则 1/2 被算成 2/3）。
8. **协议约定**：UTF-8 文本行、`\n` 分帧、单行 ≤1024 字节、聊天 ≤512 字节；
   昵称 `[A-Za-z0-9_]` 2–16 位且首字符字母数字；房号 `[0-9A-Za-z]` 1–16 位；
   15×15、黑先、任意方向 ≥5 连即胜（无禁手）、满盘和棋；
   对局中第三人 `SIT` → `GAME_RUNNING`、观战者 `CHAT` → `CHAT_LOCKED`；
   起身/断线 = 认输（`FORFEIT_STAND` / `FORFEIT_LEFT`）；单房间上限 64 人。

## 7. 环境与端口

| 项 | 值 |
|---|---|
| 系统 | Linux（Ubuntu 22.04），**只在 Linux 编译运行** |
| 工具链 | g++ 11.4、CMake 3.22、Python 3、MySQL 8.0 |
| 依赖 | build-essential、cmake、libboost-dev、libmysqlclient-dev、mysql-server、python3 |
| 数据库 | 库 `gomoku`，账号 `gomoku / gomoku123`（两套共用；初始化 `cd forDsh && ./scripts/init_db.sh`） |
| 软链 | `/forDsh` → 参考实现（不动）；`/gomoku-self` → `forMe/`（可选） |
| 端口 | 参考实现 9527/8080；自研并行用 9627/8180；**A/B 时同端口先后跑，不并行** |

## 8. 命令速查（在 `forMe/` 执行）

```bash
# 编译
./scripts/build.sh                                                    # -> build/（整个工程，自己的 muduo）
cmake -S muduo -B muduo/build && cmake --build muduo/build -j$(nproc)   # -> muduo/build/（只编 muduo，逐批验证用）
cmake -S . -B build-ref -DUSE_REFERENCE_MUDUO=ON -DMUDUO_REF_DIR=/forDsh/muduo
cmake --build build-ref -j$(nproc)                                    # -> build-ref/（参考 muduo）

# 测试
./build/bin/gomoku_board_test                # PASS（591 项）
python3 Gomoku/tests/e2e_test.py             # 38 checks, 0 failures
./scripts/progress.sh                        # muduo 进度 N/82

# 跑服务
./scripts/run_server.sh                      # 9527 / 8080
./scripts/run_client.sh --nick alice --password alice123 --register
curl -s http://127.0.0.1:8080/health         # {"status":"ok"}
curl -s http://127.0.0.1:8080/stats          # 运行统计 JSON

# 三项指标
TAG=<轮次> ./benchmarks/run_webbench.sh                                  # QPS
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000 # 最大在线
python3 benchmarks/scale_test.py latency -c 100 -t 10 \
  | tee benchmarks/results/scale-latency-<轮次>-$(date +%Y%m%d-%H%M%S).md # 延迟(PING)
python3 benchmarks/scale_test.py latency -c 50 -t 10 --payload chat --rooms 4 \
  | tee benchmarks/results/scale-chat-<轮次>-$(date +%Y%m%d-%H%M%S).md   # 延迟(CHAT)
```

`scale_test.py` 只打印到 stdout，留档用 `tee`；全局参数（`--port/--http-port/--no-start`）必须写在子命令前。
`run_webbench.sh` 认 `SERVER_BIN` / `TAG` / `NO_START=1` / `LEVELS` / `DURATION`。

## 9. 性能口径与基线

**固定口径**：同一台机器、`--io-threads 4`、每档 10 秒、`ulimit -n 65535`、日志写 `/tmp`、
压测期间不跑别的负载、A/B 同端口先后跑。

参考实现实测（原始数据 `forDsh/benchmarks/results/`，已复制为 `forMe/benchmarks/results/baseline-forDsh-*`）：

| 指标 | 结果 |
|---|---|
| webbench（HTTP 8080，c=1/10/50/100/200/500） | **0 失败**；峰值 **35128 req/s**（c=10），之后稳定约 3 万 |
| 游戏协议压测 | c=10 → 4916 msg/s；c=50 → 1895；c=200 → 641；**0 错误** |
| 最大在线客户端 | **20000** 个已认证连接（`/stats` 的 `online=20000`），未触顶 |
| PING 往返 p50 | c=1 → 0.047 ms；c=100 → 1.368 ms；c=500 → 9.650 ms；c=1000 → 21.541 ms |
| CHAT 广播 p50 / p99 | c=50 → 0.446 / 21.0 ms；c=200 → 16.149 / 81.6 ms |

解读：**认证吞吐约 210–250 次/秒**（单条 MySQL 连接 + 每事务 fsync）是"上人慢"的原因，不是网络层；
HTTP QPS 平台化是单 IO 线程 + 每请求短连接的设计使然；延迟看 **p99** 不看 max；结论只在同环境内相对比较。

## 10. 验证状态

`forMe/` 用**参考 muduo** 复跑过（证明复制过来的业务代码与原版等价）：

| 项目 | 结果 |
|---|---|
| `gomoku_board_test` | 591 项断言，0 失败，PASS |
| `Gomoku/tests/e2e_test.py` | 38 checks, 0 failures, PASS |
| webbench 冒烟（`TAG=smoke`，c=10，2s） | 0 失败，QPS 31452，结果带 tag 与二进制指纹 |

## 11. 优化候选池（阶段 D 从这里挑）

**[muduo]** = 网络库本身的改动；**[应用层]** 不算 muduo 优化，别混进对比表当 muduo 的功劳。

| # | 方向 | 类型 | 当前做法 → 方向 | 预期 / 风险 |
|---|---|---|---|---|
| 1 | 批量 accept | [muduo] | `Acceptor::handleRead` 一次只 accept 一个（上游 FIXME）→ `while(accept4(...) >= 0)` + `EPOLLET` | 突发建连 syscall 减半；ET 下漏连接风险，需测 max-online |
| 2 | 发送路径零拷贝 | [muduo] | 跨线程 `send` 走 `message.as_string()` 整块拷贝 → 传 `Buffer`/`string&&` 所有权 | 大响应体明显；注意生命周期 |
| 3 | output buffer 分段 | [muduo] | 单块 `Buffer` 积压时 `makeSpace` 是 O(n) 前移 → 链式/环形缓冲 | 慢客户端收益大；实现复杂 |
| 4 | 水位变真流控 | [muduo] | `highWaterMark_` 只通知不阻断（默认 64MB）→ 越线挂起连接 | 防 OOM；改变业务语义，要确认 |
| 5 | 唤醒合并 | [muduo] | `queueInLoop` 每次可能 `write(eventfd)` → 标志位/批量投递 | 跨线程小包收益；注意丢唤醒 |
| 6 | 定时器结构 | [muduo] | 两个互为镜像的 `set` → 时间轮或最小堆 | 高频短定时收益；需造场景 |
| 7 | poller 事件数组回收 | [muduo] | `events_` 只翻倍不缩容 → 加水位回收/上限 | 内存曲线更平；注意 resize 成本 |
| 8 | 每事件开销 | [muduo] | 每事件一次 `weak_ptr::lock` + `std::function` 间接调用 + `shared_from_this` → `tie_` 只给可能自析构的连接开 | 高 PPS 下有感；改回调签名影响面大 |
| 9 | 连接分配 | [muduo] | 只有 round-robin/hash → 按 loop 负载选 | 房间型业务易倾斜；需先造倾斜场景 |
| 10 | 认证吞吐 | [应用层] | `Db` 单连接 + 每事务 fsync → 连接池 / DB 线程 + 任务队列 | 直接抬高 max-online 斜率 |
| 11 | HTTP 端点 | [应用层] | 单 IO 线程 + 每请求短连接 → `setThreadNum(N)` 或 keep-alive | webbench 数字大涨 |

## 12. 坑、文档地图、AI 规范

**容易踩的坑**

1. **空 muduo 时编译失败是预期**：`./scripts/build.sh` 停在 `muduo/net/Buffer.h: No such file`（或 `muduo/base/Logging.h`）。想先跑业务用 `-DUSE_REFERENCE_MUDUO=ON`。
2. **三个构建目录别混用**：`forMe/build/`（整个工程，`./scripts/build.sh`）、`forMe/muduo/build/`（只编 muduo，逐批验证用）、`forMe/build-ref/`（链接参考 muduo 的对照）。三者都由 `.gitignore` 忽略，二进制不要互换。`build` 是相对路径，`cmake --build build` 必须在配置过它的那个目录里跑。
3. **先广播 OVER 后写库**：查战绩要轮询，别收到 OVER 立刻断言。
4. **e2e / 压测会写数据库**：`users`、`match_history` 会留下 `bench_`/`scale_` 账号。
5. **webbench 三个坑**：URL 必须 `/` 结尾；用默认 HTTP/1.0（别加 `-2`）；`ulimit -n 65535`。
6. **端口占用**：先 `ss -lntp | grep 9527`，再考虑换端口。
7. **脚本换行符**：仓库用 `.gitattributes` 固定 LF；若 `.sh` 报 bad interpreter，先查这一点。
8. **悬空声明**：参考实现 `StringPiece` 的 `operator<<`、`TimeZone::China()` 只有声明，照抄会链接失败。

**文档地图**

| 文档 | 作用 |
|---|---|
| `README.md`（根） | 项目总览 + **学习顺序（含当前进度）** |
| `AGENTS.md`（本文） | 规则、路线、口径、优化池、坑 |
| `forMe/README.md` | 自研工程与 forDsh 的差异、build-ref 对照构建 |
| `forMe/muduo/README.md` | 规模、增量编译、与参考实现的两处写法差异 |
| `forMe/benchmarks/RESULTS.md` | 优化前后对比台账（每轮一行） |
| `forDsh/README.md` | 功能、架构、环境、DB、编译运行、客户端、协议、测试、FAQ |
| `forDsh/muduo/README.md` | 副本来源、包含/排除、构建差异、调用链速查 |
| `forDsh/Gomoku/README.md` | 组件职责、锁边界、状态机、胜负判定 |
| `forDsh/benchmarks/README.md` | 三套压测的方法、参数与解读 |

**AI 回答规范**

1. **先给结论**（能做/不能做/为什么），再给原理与调用链，最后给"你要写的接口或伪代码"。
2. **绝不整段贴 muduo 实现**；给签名 + 注释 + 分步提示，让用户自己写。
3. 纠错要指出**具体文件与行为**（"`Channel::handleEvent` 少了 `tie_.lock()`，连接在回调里被析构就 UAF"）。
4. 谈性能先对齐口径（§9），没有数字别说"更快"。
5. 不确定就去读源码；不要凭印象编 muduo 的 API。
