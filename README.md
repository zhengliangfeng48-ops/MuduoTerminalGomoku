# 终端五子棋 CS 系统（基于 muduo 网络库）

一个**纯终端文字界面**的五子棋联机对弈系统：服务端用陈硕 [muduo](https://github.com/chenshuo/muduo) 的
Reactor 网络库实现，客户端是同仓库的终端程序；账号与战绩（场次 / 胜率）持久化到 MySQL；
并附 **webbench** 压力测试与实测数据。

本仓库同时是**一个 muduo 学习工程**：`forDsh/` 是已完成并验证的参考实现，
另一个目录是"照着它自己重写 muduo、再测性能、再做优化"的工作区。

| 部分 | 状态 |
|---|---|
| `forDsh/` 参考实现 | ✅ 完成并验证（单测 591 项、e2e 38 项、webbench 6 档 0 失败、2 万在线） |
| `基于改进muduo高性能网络库的终端五子棋对弈CS模型/` 自研实现 | 🚧 骨架就绪，muduo 手写进度 0/82 |

---

## 目录结构

```
├── README.md                 # 本文件：项目总览
├── AGENTS.md                 # 给 AI/新会话的工作说明（规则、学习路线、性能口径、坑）
├── forDsh/                   # ① 参考实现（冻结，不改）
│   ├── README.md             #   最全的说明：目录 / DB / 协议 / 测试 / 压测 / FAQ
│   ├── muduo/                #   muduo 核心库（上游逐字副本，82 源文件）
│   ├── Gomoku/               #   服务端 + 终端客户端 + 协议 + 单测 + e2e
│   ├── scripts/              #   setup_env / build / init_db / run_server / run_client
│   ├── benchmarks/           #   webbench 矩阵、协议压测、规模与延迟压测 + 实测结果
│   └── tools/webbench/       #   内置 webbench 1.5
└── 基于改进muduo高性能网络库的终端五子棋对弈CS模型/   # ② 自研实现（学习用）
    ├── README.md             #   与 forDsh 的全部差异
    ├── muduo/                #   要手写：README 里有 82 文件清单与实现顺序
    ├── Gomoku/               #   从 forDsh 原样复制（同一份业务代码）
    ├── scripts/              #   + progress.sh（学习进度）、setup_env 不抢 /forDsh
    └── benchmarks/           #   + RESULTS.md（优化前后对比台账）、webbench 支持 TAG
```

两套实现**共用同一个 MySQL 库**（`gomoku`），所以性能可以直接对照。

---

## 架构

```
        gomoku_client（终端，TcpClient + stdin 线程）
                     │  TCP 文本行协议（\n 分帧）
                     ▼
  ┌──────────────────────────────────────────┐
  │ gomoku_server                            │
  │  TcpServer(9527) ─ EventLoopThreadPool   │  ← 游戏协议（多 IO 线程）
  │     └ Session / RoomManager / Room       │
  │         └ GomokuGame（15×15 纯逻辑）      │
  │  TcpServer(8080) ─ 独立 EventLoopThread  │  ← 极简 HTTP：/ /health /stats
  │  AuthService ─ Db（MySQL C API，事务）     │
  └───────────────────┬──────────────────────┘
                      ▼  MySQL 8
              users / match_history

  webbench ──► http://127.0.0.1:8080/   压真实的 accept/epoll/读写/关闭路径
```

线程模型要点：`main` 跑 base loop 负责 `accept`；`setThreadNum(N)` 起 N 个 IO 线程，
**每条连接的回调只在它自己的 IO 线程执行**；跨线程共享的三处（房间表、Auth/Db、在线表）各有锁；
房间广播走 `TcpConnection::send()`，muduo 内部负责唤醒目标 loop；HTTP 端点单独一个线程，压测不干扰对局。

---

## 快速开始（WSL2 Ubuntu）

```bash
cd /forDsh                      # 参考实现（软链；实体路径见 AGENTS.md）
./scripts/build.sh              # 编译 muduo + 服务端 + 客户端 + 单测
./build/bin/gomoku_board_test   # 单元测试：591 项断言，末行 PASS
python3 Gomoku/tests/e2e_test.py    # 端到端：自动拉起临时服务端，38 checks / 0 failures
./scripts/run_server.sh         # 游戏 9527 / 压测 HTTP 8080
./scripts/run_client.sh --nick alice --password alice123 --register
curl -s http://127.0.0.1:8080/health    # {"status":"ok"}
```

数据库（首次或清空后）：`./scripts/init_db.sh`，默认账号 `gomoku / gomoku123`，库名 `gomoku`。
依赖：`build-essential cmake libboost-dev libmysqlclient-dev mysql-server python3`。

客户端常用命令：`/join <房间号>`（不存在自动创建）、`/sit`（两座坐满自动开局）、`H8` 或 `8 8` 落子、
`/chat <文本>`、`/stand`、`/ready`（再来一局）、`/who`、`/rooms`、`/stats`。
对局中只有对弈双方能聊天（观战者 `ERR CHAT_LOCKED`），起身或断线即认输。

---

## 性能实测（参考实现，WSL2 / i5-13500H / 16 核）

| 指标 | 结果 |
|---|---|
| webbench（HTTP 8080，并发 1/10/50/100/200/500，每档 10s） | **6 档全部 0 失败**；峰值 **35128 req/s**（c=10），之后稳定约 3 万 |
| 游戏协议压测 | c=10 → 4916 msg/s，c=50 → 1895，c=200 → 641，**0 错误** |
| 最大在线客户端 | **20000** 个已认证连接（`/stats` 的 `online=20000`），未触顶 |
| 客户端往返延迟（PING）p50 | c=1 → **0.047 ms**；c=100 → 1.368 ms；c=500 → 9.650 ms；c=1000 → 21.541 ms |
| 聊天广播延迟 | c=50 p50 0.446 ms / p99 21.0 ms；c=200 p50 16.1 ms / p99 81.6 ms |

复跑命令与结果文件见 `forDsh/benchmarks/README.md`；原始数据在 `forDsh/benchmarks/results/`。
结论：连接能力不是瓶颈（真正的约束是 `ulimit -n`），**认证吞吐（约 210–250 次/秒，受单条 MySQL 连接与事务 fsync 限制）**
才是"上人慢"的原因；HTTP QPS 平台化则是"单 IO 线程 + 每请求短连接"的设计使然。

---

## 实现要点（容易踩的坑）

1. **先广播 `OVER`、后写数据库**：结算在房间锁内产生结果，写库放到锁外，避免慢 SQL 拖住所有房间；
   代价是收到 `OVER` 时库可能还没写完，所以测试要轮询校验战绩。
2. **MySQL 两个坑**：胜率必须写 `games + 1.0`（否则整数除法）；`SET` 从左到右求值，
   所以 `win_rate` 必须写在 `games` / `wins` **之前**。
3. **同进程两个 TcpServer**：`TcpServer::start()` 必须投递到它自己的 loop 线程执行，否则断言崩溃。
4. **Session 只持连接的 `weak_ptr`**：反向强持有会成环泄漏。
5. 协议：UTF-8 文本行、单行 ≤1024 字节；昵称 `[A-Za-z0-9_]` 2–16 位；房号 `[0-9A-Za-z]` 1–16 位；
   15×15、黑先、任意方向 ≥5 连即胜（无禁手）、满盘和棋；单房间上限 64 人。
6. 密码目前**明文存储**（按需求），协议未加密，仅用于本机/局域网演示；muduo 为上游逐字副本，未做优化。

---

## 文档索引

| 文档 | 内容 |
|---|---|
| [`AGENTS.md`](AGENTS.md) | **新会话/AI 必读**：硬规则、学习路线、性能口径、优化候选、坑 |
| [`forDsh/README.md`](forDsh/README.md) | 参考实现完整说明：目录、数据库、编译启动、协议、测试、压测、FAQ |
| [`forDsh/muduo/README.md`](forDsh/muduo/README.md) | muduo 副本的来源/包含/排除、与上游构建差异、核心调用链速查 |
| [`forDsh/Gomoku/README.md`](forDsh/Gomoku/README.md) | 组件职责、锁边界、房间状态机、MySQL 事务 |
| [`forDsh/benchmarks/README.md`](forDsh/benchmarks/README.md) | 三套压测的方法、参数与解读 |
| [`基于改进……/README.md`](基于改进muduo高性能网络库的终端五子棋对弈CS模型/README.md) | 自研工程说明：与 forDsh 的差异、逐步编译 |
| [`基于改进……/muduo/README.md`](基于改进muduo高性能网络库的终端五子棋对弈CS模型/muduo/README.md) | 82 个文件的实现清单与依赖顺序 |
| [`基于改进……/benchmarks/RESULTS.md`](基于改进muduo高性能网络库的终端五子棋对弈CS模型/benchmarks/RESULTS.md) | 优化前后对比台账 |

---

## 许可证

- `forDsh/muduo/` 与自研目录的 muduo：遵循 **BSD 3-Clause**（见各自目录下 `License`），著作权归 Shuo Chen。
- `tools/webbench/`：遵循 **GPL v3**，原作者 Radim Kolar。
- 本项目的 Gomoku、脚本与文档用于学习与演示。
