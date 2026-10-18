# Gomoku 终端五子棋（基于 muduo 网络库）

> 一个**纯终端文字界面**的五子棋联机对弈系统：服务端基于陈硕 muduo 的 Reactor 网络库实现，
> 客户端是同仓库的终端程序；账号与战绩（场次 / 胜率）持久化到 MySQL。
> 并附带 **webbench** 压力/性能测试脚本与实测数据。

---

## 目录

1. [功能一览](#1-功能一览)
2. [系统架构](#2-系统架构)
3. [目录结构](#3-目录结构)
4. [环境准备](#4-环境准备)
5. [muduo 核心库说明](#5-muduo-核心库说明)
6. [数据库配置](#6-数据库配置)
7. [编译](#7-编译)
8. [启动服务端](#8-启动服务端)
9. [客户端使用](#9-客户端使用)
10. [通信协议参考](#10-通信协议参考)
11. [功能测试](#11-功能测试)
12. [压力与性能测试](#12-压力与性能测试)
13. [常见问题 FAQ](#13-常见问题-faq)
14. [已知取舍与限制](#14-已知取舍与限制)
15. [许可证与致谢](#15-许可证与致谢)

---

## 1. 功能一览

| 需求 | 实现 |
|---|---|
| 输入房间号进入房间 | `JOIN <房间号>`，房间不存在则**自动创建**（房号 1-16 位字母数字） |
| 房间内无人对弈即可上座 | `SIT` 自动占第一个空座（先黑后白）；两座坐满**立即自动开局** |
| 非对弈时间所有客户端均可在房间聊天 | 对局未开始时，房间内**所有人**（含观战者）都可 `CHAT` |
| 对弈时间只有对弈双方可发送信息 | 对局进行中，观战者发 `CHAT` 返回 `ERR CHAT_LOCKED`；对弈双方正常广播 |
| 启动客户端需要注册 | 客户端启动先选择"注册/登录"，没有账号必须先注册 |
| 昵称不能重复 | 服务端先查重，DB 上还有 `UNIQUE KEY uk_nickname` 兜底 |
| 密码暂时直接存入 | `users.password` 明文存储（按需求，README 标注后续可替换为哈希） |
| 场次 / 胜率入库 | 每局结束在**一个事务**里更新双方 `games / wins / win_rate` 并写 `match_history` |
| 纯终端文字画面 | 15×15 文字棋盘 + 房间头部 + 聊天/系统消息，ANSI 颜色可选 |
| 压力 / 性能测试 | 内置 webbench 压 HTTP 端点 + Python 压游戏协议，结果见 `benchmarks/results/` |
| 注释清晰 | 所有 Gomoku 代码为中文注释，重点解释 Reactor 调用链、多线程共享状态、协议状态机、胜负判定、MySQL 事务 |

其它附加能力：`READY` 再来一局、起身/断线判负、`ROOMS` 房间列表、`WHO` 成员、
`STATS` 我的战绩、`/health` 与 `/stats` HTTP 端点。

---

## 2. 系统架构

```
            ┌─────────────────────────────┐
            │        gomoku_client        │   （终端程序，TcpClient + stdin 线程）
            │  棋盘渲染 / 命令解析 / 聊天  │
            └──────────────┬──────────────┘
                           │  TCP 文本协议（\n 分帧）
                           ▼
   ┌────────────────────────────────────────────────────────┐
   │                    gomoku_server                       │
   │                                                        │
   │  TcpServer(9527)  ── EventLoopThreadPool（N 个 IO 线程）│
   │        │              ├─ Session（每连接状态机）        │
   │        │              └─ RoomManager（全局互斥）        │
   │        │                     └─ Room（座位/对局/广播）   │
   │        │                            └─ GomokuGame       │
   │        │                                                │
   │  TcpServer(8080)  ── 独立 EventLoopThread               │
   │        └─ 极简 HTTP：/  /health  /stats                 │
   │                                                        │
   │  AuthService ── Db（MySQL C API，事务）                  │
   └───────────────┬────────────────────────────────────────┘
                   │ MySQL 协议（3306）
                   ▼
            ┌─────────────┐
            │   MySQL 8   │  users / match_history
            └─────────────┘

   webbench ──────► http://127.0.0.1:8080/  （压 Reactor 的 accept/读写路径）
```

**线程模型要点**

- `main` 线程运行 base `EventLoop`，负责 `accept`；
- `TcpServer::setThreadNum(N)` 起 N 个 IO 线程，连接按轮转绑定，**每条连接的回调只在自己的 IO 线程执行**；
- 跨线程共享的只有三处，各有明确保护：`RoomManager`（mutex）、`AuthService/Db`（mutex）、在线表与计数（mutex + atomic）；
- 房间广播最终走 `TcpConnection::send()`，muduo 内部会排队并唤醒目标 loop，本身就线程安全；
- HTTP 端点单独一个 `EventLoopThread`，避免压测流量影响对局。

---

## 3. 目录结构

```
forDsh/
├── README.md                     # 本文件
├── CMakeLists.txt                # 顶层工程（muduo + Gomoku）
├── .gitignore
├── muduo/                        # muduo 核心库（逐字复制上游，仅新增构建脚本）
│   ├── README.md                 # 来源 / 包含 / 排除 / 与上游差异
│   ├── License                   # BSD 3-Clause（原样）
│   ├── CMakeLists.txt
│   └── muduo/
│       ├── base/                 # 日志、线程、时间、文件…（42 个 .h/.cc）
│       └── net/                  # Reactor / TCP / Timer（35 个 .h/.cc）
│           └── poller/           # EPollPoller / PollPoller / DefaultPoller
├── Gomoku/
│   ├── README.md                 # 组件设计说明
│   ├── CMakeLists.txt
│   ├── common/Protocol.{h,cc}    # 协议常量、校验、工具
│   ├── server/
│   │   ├── main.cc               # 入口：参数/日志/事件循环
│   │   ├── Config.{h,cc}         # 命令行 + GOMOKU_* 环境变量
│   │   ├── ServerApp.{h,cc}      # TcpServer 组装、命令分发、在线表、统计
│   │   ├── Session.{h,cc}        # 每连接协议状态机
│   │   ├── RoomManager.{h,cc}    # 房间表 + 全局互斥
│   │   ├── Room.{h,cc}           # 座位 / 聊天可见性 / 对局生命周期 / 广播
│   │   ├── GomokuGame.{h,cc}     # 15×15 棋盘与胜负判定（纯逻辑）
│   │   ├── AuthService.{h,cc}    # 注册 / 登录 / 战绩事务
│   │   ├── Db.{h,cc}             # MySQL C API 封装（互斥、ping 重连、转义）
│   │   └── HttpStatusServer.{h,cc}# 压测用 HTTP 端点
│   ├── client/
│   │   ├── main.cc               # 登录引导 + 事件循环
│   │   ├── GameClient.{h,cc}     # TcpClient 封装、消息分发、本地命令
│   │   ├── TerminalUI.{h,cc}     # 棋盘 / 房间头 / 聊天渲染
│   │   └── StdinLoop.{h,cc}      # stdin 线程 → runInLoop 投递
│   ├── sql/schema.sql            # 建库建表建账号
│   └── tests/
│       ├── board_test.cc         # 棋盘/协议单元测试（591 项断言）
│       └── e2e_test.py           # 协议级端到端测试（38 项断言）
├── tools/webbench/               # webbench 1.5（含一处可移植性补丁）+ BUILD.md
├── scripts/
│   ├── setup_env.sh              # 安装依赖 + /forDsh 软链
│   ├── build.sh                  # 一键编译
│   ├── init_db.sh                # 启库 + 建表 + 建账号 + 自检
│   ├── run_server.sh
│   └── run_client.sh
└── benchmarks/
    ├── README.md                 # 压测方法说明
    ├── run_webbench.sh           # 并发矩阵 → Markdown + CSV
    ├── gomoku_bench.py           # 游戏协议并发压测
    ├── scale_test.py             # 最大在线客户端数 + 客户端往返延迟
    └── results/                  # 实测结果（含原始输出）
```

构建产物（`.gitignore` 已忽略）：

```
build/lib/libmuduo_base.a  libmuduo_net.a
build/bin/gomoku_server  gomoku_client  gomoku_board_test
```

---

## 4. 环境准备

### 4.1 环境说明：Windows + WSL2

本项目**只在 Linux 上编译运行**（muduo 的核心是 epoll / timerfd / eventfd，
``CurrentThread.cc`` 用到 `execinfo.h`，``Thread.cc`` 用到 `linux/unistd.h`，不做 Windows 原生移植）。

当前部署形态：

| 角色 | 位置 |
|---|---|
| 源码真实路径 | `D:\coding\仿muduo网络库\forDsh` |
| WSL 内访问路径 | `/mnt/d/coding/仿muduo网络库/forDsh` |
| 便捷软链（推荐工作路径） | `/forDsh` → 上面那个目录 |

``scripts/setup_env.sh`` 会自动建立 `/forDsh` 软链，所以下文命令统一用 `/forDsh` 开头。

### 4.2 一键安装依赖

```bash
wsl -d Ubuntu-22.04 -- bash -lc "cd /forDsh && ./scripts/setup_env.sh"
```

脚本会执行：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config \
                        libboost-dev libmysqlclient-dev \
                        mysql-server python3
sudo ln -sfn "<项目目录>" /forDsh
```

依赖说明：

| 依赖 | 用途 |
|---|---|
| g++ ≥ 7 / cmake ≥ 3.10 / make | 编译 |
| libboost-dev | muduo 需要 `boost::any`、`boost::circular_buffer`、`boost::operators`（**头文件即可**） |
| libmysqlclient-dev | MySQL C API（``<mysql/mysql.h>``） |
| mysql-server | 本地数据库（也可改用外部实例，见第 6 节） |
| python3 | 端到端测试 + 协议压测 |

### 4.3 版本自检

```bash
g++ --version | head -1        # g++ (Ubuntu 11.4.0-1ubuntu1-22.04)
cmake --version | head -1      # cmake version 3.22.1
mysql --version                # mysql  Ver 8.0.4x
systemctl is-active mysql      # active
```

---

## 5. muduo 核心库说明

`muduo/` 目录是 [chenshuo/muduo](https://github.com/chenshuo/muduo) master 分支**核心源码的逐字副本**，
**没有做任何逻辑修改**（保留原版权头与英文注释），只新增了三份 `CMakeLists.txt` 和一份说明。
详细清单见 [`muduo/README.md`](muduo/README.md)。

- **包含**：全部 `base/`（日志/线程/时间/文件/队列）+ `net/` 的 Reactor 与 TCP 核心（EventLoop、Channel、Poller、EPollPoller、TcpConnection、TcpServer、TcpClient、Buffer、TimerQueue…）+ `net/poller/`。
- **排除**：`net/http`、`net/inspect`、`net/protobuf`、`net/protorpc`、`net/tests`、`base/tests`、`examples/`，以及依赖 zlib 的 `GzipFile.h` / `ZlibStream.h` 和上游占位文件 `boilerplate.*`。
- **构建差异**：仅去掉 `-Werror` / `-Wconversion` / `-Wold-style-cast`（gcc 11.3 下上游遗留告警会中断编译），其余编译选项照搬上游。

> 说明：需求是"参考 muduo 把核心代码写在 /forDsh/muduo 里、不需要改进和优化"，
> 因此这里采用**原样落地上游实现**的方式，而不是自行简化重写。

---

## 6. 数据库配置

### 6.1 初始化（使用 WSL 内 MySQL）

```bash
cd /forDsh
./scripts/init_db.sh
```

脚本等价于：

```bash
systemctl start mysql || service mysql start   # Ubuntu 的 root 用 auth_socket，无需密码
mysql < Gomoku/sql/schema.sql
```

### 6.2 表结构

```sql
CREATE DATABASE gomoku DEFAULT CHARACTER SET utf8mb4;

CREATE TABLE users (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  nickname      VARCHAR(32)  NOT NULL,          -- 昵称，全局唯一
  password      VARCHAR(64)  NOT NULL,          -- 需求：暂明文存储
  games         INT UNSIGNED NOT NULL DEFAULT 0,-- 总场次
  wins          INT UNSIGNED NOT NULL DEFAULT 0,-- 胜场
  win_rate      DECIMAL(5,4) NOT NULL DEFAULT 0,-- 胜率 = wins/games
  created_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login_at TIMESTAMP    NULL DEFAULT NULL,
  UNIQUE KEY uk_nickname (nickname)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE match_history (                     -- 对局历史（便于核对/复盘）
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  room_id VARCHAR(16) NOT NULL,
  black_nick VARCHAR(32) NOT NULL,
  white_nick VARCHAR(32) NOT NULL,
  result ENUM('black','white','draw') NOT NULL,
  reason VARCHAR(24) NOT NULL,                   -- FIVE/FULL/FORFEIT_STAND/FORFEIT_LEFT
  moves SMALLINT UNSIGNED NOT NULL,
  ended_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_black (black_nick), KEY idx_white (white_nick), KEY idx_ended (ended_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

服务端默认用账号 `gomoku` / 密码 `gomoku123` 连接 `gomoku` 库。
**生产环境请改密码**，并同步到启动参数 `--db-pass`（或环境变量 `GOMOKU_DB_PASS`）。

### 6.3 战绩是怎么更新的

一局结束（`OVER` 广播之后）由 `AuthService::recordResult()` 在**一个事务**里完成：

```sql
START TRANSACTION;
UPDATE users SET win_rate = (wins + W) / (games + 1.0),  -- 注意顺序！
                games    = games + 1,
                wins     = wins + W
WHERE id = ?;
INSERT INTO match_history (...) VALUES (...);
COMMIT;
```

两个关键点（代码注释里也有强调）：

1. 必须写 `games + 1.0`，否则 MySQL 的整数除法会把胜率算成 0 或 1；
2. MySQL 的 `SET` 子句**从左到右求值**，所以 `win_rate` 必须写在 `games` / `wins` 之前，
   否则它读到的是已经被 +1 的新值，胜率会偏大（例如 1/2 被算成 2/3）。

### 6.4 改用外部 / 远程 MySQL

例如使用 Windows 主机上已有的 MySQL（WSL2 里可以用默认网关地址访问到）：

```bash
# 1) 在 Windows 的 MySQL 里建库建账号（用 root 登录后执行 Gomoku/sql/schema.sql 中
#    CREATE DATABASE / CREATE TABLE 部分，并把账号换成 gomoku@'%'）
# 2) 从 WSL 找到 Windows 主机地址
ip route | awk '/default/ {print $3}'    # 例如 172.22.48.1
# 3) 启动服务端时指向它
./build/bin/gomoku_server --db-host 172.22.48.1 --db-port 3306 \
                          --db-user gomoku --db-pass gomoku123 --db-name gomoku
```

对应环境变量：`GOMOKU_DB_HOST` / `GOMOKU_DB_PORT` / `GOMOKU_DB_USER` / `GOMOKU_DB_PASS` / `GOMOKU_DB_NAME`。

> 数据库不可用时服务端仍能启动，只是注册/登录会返回 `ERR DB_UNAVAILABLE`，对局本身不受影响
> （战绩写库失败只记日志）。

---

## 7. 编译

```bash
cd /forDsh
./scripts/build.sh
```

等价于：

```bash
cmake -S /forDsh -B /forDsh/build -DCMAKE_BUILD_TYPE=Release
cmake --build /forDsh/build -j$(nproc)
ls -l /forDsh/build/bin
# gomoku_server  gomoku_client  gomoku_board_test
# 以及 build/lib/libmuduo_base.a  build/lib/libmuduo_net.a
```

可选：也支持单独编译 muduo 库

```bash
cd /forDsh/muduo && cmake -S . -B build && cmake --build build -j$(nproc)
```

---

## 8. 启动服务端

```bash
cd /forDsh
./scripts/run_server.sh                       # 默认 游戏端口 9527 / HTTP 8080
./scripts/run_server.sh --port 9000 --http-port 8081 --io-threads 8
```

### 8.1 参数表

| 参数 | 默认 | 说明 |
|---|---|---|
| `--listen` | `0.0.0.0` | 监听地址 |
| `--port` | `9527` | 游戏协议端口 |
| `--http-port` | `8080` | 压测 HTTP 端口（webbench 用） |
| `--io-threads` | `4` | TcpServer 的 IO 线程数；`0` 表示全部跑在 base loop |
| `--log-dir` | `logs` | 日志目录（AsyncLogging 滚动写），传空串则只输出到终端 |
| `--db-host` | `127.0.0.1` | MySQL 地址 |
| `--db-port` | `3306` | MySQL 端口 |
| `--db-user` | `gomoku` | MySQL 用户 |
| `--db-pass` | `gomoku123` | MySQL 密码 |
| `--db-name` | `gomoku` | 库名 |
| `-h, --help` | – | 帮助 |

环境变量形式：`GOMOKU_LISTEN / GOMOKU_PORT / GOMOKU_HTTP_PORT / GOMOKU_IO_THREADS / GOMOKU_LOG_DIR /
GOMOKU_DB_HOST / GOMOKU_DB_PORT / GOMOKU_DB_USER / GOMOKU_DB_PASS / GOMOKU_DB_NAME`（命令行优先级更高）。

### 8.2 启动成功的样子

```
20261018 07:29:59 INFO  ==== gomoku_server starting ====
20261018 07:29:59 INFO  MySQL connected: gomoku@127.0.0.1:3306/gomoku
20261018 07:29:59 INFO  游戏服务监听 0.0.0.0:9527（IO 线程数 4）
20261018 07:29:59 INFO  压测端点监听 0.0.0.0:8080（GET /health, /stats）
```

自检两端是否正常：

```bash
curl -s http://127.0.0.1:8080/health     # {"status":"ok"}
curl -s http://127.0.0.1:8080/stats      # JSON 运行统计
# 游戏端口可用 nc / telnet 手工调试：
nc 127.0.0.1 9527
REGISTER alice alice123
JOIN 1001
SIT
```

---

## 9. 客户端使用

```bash
cd /forDsh
./scripts/run_client.sh                              # 交互式：选择注册/登录
./scripts/run_client.sh --nick alice --password alice123 --register
./scripts/run_client.sh --host 127.0.0.1 --port 9527 --nick bob --password bob123 --login
```

### 9.1 界面

```
房间 1001  [PLAYING]   黑 X: alice   白 O: bob   轮到: bob   战绩: 1/1 (1.0000)
    A B C D E F G H I J K L M N O
 1  X . . . . . . . . . . . . . .
 2  . X . . . . . . . . . . . . .
 3  . . X . . . . . . . . . . . .
 4  . . . X . . . . . . . . . . .
 5  . . . . . . . . . . . . . . .
 ...
15  . . . . . . . . . . . . O O O
最后一手: D4
房间成员: alice,bob
输入 H8 或 8 8 落子；/chat 文本 聊天；/stand 起身；/help 查看全部命令
> 
```

### 9.2 命令表

| 输入 | 作用 |
|---|---|
| `/join <房间号>` | 进入房间（不存在自动创建） |
| `/leave` | 离开房间（对局中离开 = 认输） |
| `/sit` | 上座（房间无对局且有空座时） |
| `/stand` | 起身（对局中起身 = 认输） |
| `/ready` | 上一局结束后准备下一局（双方都 READY 才开新局） |
| `H8` 或 `8 8` | 落子：列 A-O + 行 1-15 |
| `/move <列> <行>` | 落子的另一种写法 |
| `/chat <文本>` | 发聊天（对局中只有对弈双方可发） |
| `/board` | 重绘棋盘 |
| `/who` / `/rooms` / `/stats` | 房间成员 / 房间列表 / 我的战绩 |
| `/clear` / `/quit` / `/help` | 清屏 / 退出 / 帮助 |
| 其它任意文本 | 等同 `/chat`，直接发聊天 |

### 9.3 典型对局流程

```bash
# 终端 1
./scripts/run_client.sh --nick alice --password alice123 --register
> /join 1001
> /sit

# 终端 2
./scripts/run_client.sh --nick bob --password bob123 --register
> /join 1001
> /sit              # 两人都坐下后自动开局，黑方先走

# 终端 1（黑方）
> E5
# 终端 2（白方）
> F6
...
# 分出胜负后
> /ready           # 双方都输入 /ready 即开始下一局
```

---

## 10. 通信协议参考

**传输**：TCP 上的 UTF-8 文本行协议，一行一条消息，以 `\n` 分隔（服务端会自动剥除 `\r`，可直接用 nc 调试）。
单行上限 1024 字节，聊天正文上限 512 字节。

### 10.1 客户端 → 服务端

| 命令 | 说明 |
|---|---|
| `REGISTER <昵称> <密码>` | 注册并直接登录 |
| `LOGIN <昵称> <密码>` | 登录 |
| `JOIN <房间号>` | 进房 / 自动建房 |
| `LEAVE` | 离开房间 |
| `SIT` / `STAND` | 上座 / 起身（对局中 = 认输） |
| `READY` | 准备下一局 |
| `MOVE <列> <行>` | 落子，列行均为 1-15 |
| `CHAT <文本>` | 聊天 |
| `BOARD` / `WHO` / `ROOMS` / `STATS` | 查询 |
| `PING` / `QUIT` | 心跳 / 断开 |

昵称规则：2-16 位 `[A-Za-z0-9_]`，首字符必须是字母或数字；密码：4-32 位可打印 ASCII，不含空格；
房间号：1-16 位 `[0-9A-Za-z]`。

### 10.2 服务端 → 客户端

| 消息 | 说明 |
|---|---|
| `OK <msg>` / `ERR <CODE> <msg>` | 通用应答 |
| `AUTH <昵称> <场次> <胜场> <胜率>` | 登录成功 + 战绩 |
| `ROOM <房号> <状态> <黑> <白> <自己>` | 房间快照（状态 IDLE / WAITREADY / PLAYING） |
| `USERS <n1,n2,...>` | 房间成员 |
| `JOINED <昵称>` / `LEFT <昵称>` | 成员变化 |
| `SEATS <黑> <白> <状态>` | 座位变化（空位用 `-` 表示） |
| `START <黑> <白>` | 开局 |
| `TURN <昵称>` | 轮到谁 |
| `PLACED <列> <行> <昵称> <B\|W>` | 落子 |
| `BOARD <225 字符> <最后列> <最后行>` | 棋盘快照（`.` / `X` / `O`，行优先） |
| `OVER <WON\|DRAW> <胜者> <原因>` | 结束；原因见下 |
| `INFO <场次> <胜场> <胜率>` | 战绩刷新 |
| `CHAT <昵称> <文本>` / `SYS <文本>` | 聊天 / 系统消息 |
| `ROOMS <房号:状态:黑:白:人数\|...>` | 房间列表 |
| `PONG` | 心跳应答 |

结束原因：`FIVE`（五连）、`FULL`（满盘和棋）、`FORFEIT_STAND`（起身认输）、`FORFEIT_LEFT`（断线判负）。

### 10.3 错误码

`NOT_LOGIN`、`BAD_CMD`、`BAD_ARG`、`NAME_TAKEN`、`NO_SUCH_USER`、`BAD_PASSWORD`、
`ALREADY_ONLINE`、`ALREADY_AUTHED`、`NOT_IN_ROOM`、`ALREADY_IN_ROOM`、`GAME_RUNNING`、
`SEAT_FULL`、`NOT_SEATED`、`NOT_YOUR_TURN`、`ILLEGAL_MOVE`、`CHAT_LOCKED`、
`DB_UNAVAILABLE`、`LINE_TOO_LONG`、`ROOM_FULL`。

---

## 11. 功能测试

### 11.1 棋盘与协议单元测试

```bash
cd /forDsh
./build/bin/gomoku_board_test
```

覆盖：坐标校验、初始状态、横向/纵向/两条斜线五连、长连（六连）、满盘和棋（构造了
一个"113 黑 + 112 白、四方向最长同色 4 连"的染色来真实跑满 225 手）、非法落子、
认输结束与重置、棋盘序列化、昵称/密码/房号校验、胜率格式化、坐标解析。
最后一行为 `PASS`（共 591 项断言）。

### 11.2 协议级端到端测试

```bash
cd /forDsh
python3 Gomoku/tests/e2e_test.py
```

脚本会**自动拉起一个临时服务端**（端口 19527/18080，独立日志目录），用原始 socket 完成 11 组场景：

1. 未登录命令被拒（`NOT_LOGIN`）
2. 注册两位玩家，校验初始战绩
3. 昵称唯一（`NAME_TAKEN`）、重复在线（`ALREADY_ONLINE`）、错误密码（`BAD_PASSWORD`）、不存在用户（`NO_SUCH_USER`）
4. 进房自动建房、上座、坐满自动开局、黑方先手
5. 轮次校验（`NOT_YOUR_TURN`）、越界坐标（`BAD_ARG`）、占位落子（`ILLEGAL_MOVE`）
6. 五连判胜 + **MySQL 战绩校验**（games / wins / win_rate / match_history）
7. 赛后 `READY` 重开；观战者上座被拒（`GAME_RUNNING`）、观战者聊天被拒（`CHAT_LOCKED`）、对弈双方聊天可广播
8. 起身认输（`FORFEIT_STAND`）+ 战绩校验
9. 断线判负（`FORFEIT_LEFT`）+ 战绩校验
10. `ROOMS` / `WHO`、`ALREADY_IN_ROOM`、非法房号、`NOT_IN_ROOM`
11. HTTP 端点 `/health`、`/stats`

结尾打印 `===== 38 checks, 0 failures =====` 与 `PASS`。

> 说明：服务端是"先广播 `OVER`、再写库"（避免在房间锁里做阻塞 IO），
> 所以测试里对战绩的断言采用**轮询等待**（`wait_stats`），而不是收到 `OVER` 立刻查库。

### 11.3 客户端冒烟测试（可选）

```bash
# 用管道喂脚本化输入，检查渲染是否正常
{ sleep 1; echo "/join 9001"; sleep 0.5; echo "/sit"; sleep 0.5; echo "H8"; sleep 1; echo "/quit"; } \
  | ./build/bin/gomoku_client --nick smoke --password smoke123 --register --no-color
```

---

## 12. 压力与性能测试

### 12.1 为什么用 webbench 压一个"非 HTTP"的服务端？

webbench 只会发 HTTP 请求，而游戏业务是自定义文本协议。为此服务端在**同进程内**
额外起了一个极简 HTTP `TcpServer`（默认 8080，独立 EventLoopThread），只做三件事：

| 路径 | 响应 |
|---|---|
| `/` | 很小的 HTML 页面 |
| `/health` | `{"status":"ok"}` |
| `/stats` | 运行统计 JSON（连接数、在线、房间数、对局数、注册用户数、uptime、DB 状态） |

这样 webbench 压的就是**真实的 muduo accept/epoll/读写/关闭路径**，
且压测流量与游戏流量线程隔离，不会污染协议。

### 12.2 编译 webbench

```bash
cd /forDsh/tools/webbench
make            # 产物 ./webbench
```

> 本次内置的 webbench 相对上游只有一处改动：删除 `#include <rpc/types.h>`
> （Ubuntu 22.04 / glibc 2.35 已不再提供该头，且源码中并未使用）。
> 详见 [`tools/webbench/BUILD.md`](tools/webbench/BUILD.md)。

### 12.3 跑并发矩阵

```bash
cd /forDsh
./benchmarks/run_webbench.sh
# 自定义档位与时长
DURATION=5 LEVELS="10 100 1000" ./benchmarks/run_webbench.sh
# 复用已在运行的服务端
NO_START=1 ./benchmarks/run_webbench.sh
```

脚本会自动：启动服务端（若端口空闲）→ 提高 `ulimit -n` → 逐档压测 →
解析 webbench 的 `Speed=... pages/min` 与 `Requests: N susceed, N failed` →
输出 Markdown + CSV + 每档原始输出到 `benchmarks/results/`。

单项手压也可以：

```bash
ulimit -n 65535
./tools/webbench/webbench -c 100 -t 10 http://127.0.0.1:8080/
```

注意 webbench 的两个坑：
- URL **必须以 `/` 结尾**（`http://host:port/`）；
- 必须用默认的 HTTP/1.0（`-1`）。服务端应答后主动关连接，webbench 读到 EOF 才计数，
  用 `-2` 会因为不保持长连接而产生大量失败。

### 12.4 实测结果（webbench）

环境：i5-13500H / 16 核 / 7810 MB / WSL2 Ubuntu 22.04，内核 6.6.114.1；
服务端 `--io-threads 4`，单档 10 秒，目标 `http://127.0.0.1:8080/`。
（原始文件：`benchmarks/results/webbench-20261018-153703.md`）

| 并发 | QPS (req/s) | 成功请求 | 失败请求 | 传输速率 (MB/s) |
|---:|---:|---:|---:|---:|
| 1 | 14067 | 140673 | 0 | 5.02 |
| 10 | **35128** | 351277 | 0 | 12.53 |
| 50 | 30036 | 300363 | 0 | 10.71 |
| 100 | 30420 | 304196 | 0 | 10.85 |
| 200 | 29901 | 299012 | 0 | 10.66 |
| 500 | 30489 | 304887 | 0 | 10.87 |

**结论**：6 档并发全部 **0 失败**。QPS 在 `c=10` 达到峰值约 3.5 万，之后稳定在 3 万左右
（说明瓶颈在"每请求一次 connect/close + 单线程 HTTP 端点"，而不是连接数）；
单请求成本约 33 微秒。想继续提升需要让 HTTP 端点也开多 IO 线程、或改用长连接，
这属于优化范畴，本项目按需求**不做**。

### 12.5 游戏协议压测（webbench 覆盖不到的部分）

```bash
cd /forDsh
ulimit -n 65535
python3 benchmarks/gomoku_bench.py -c 50 -t 10 --rooms 4
python3 benchmarks/gomoku_bench.py -c 200 -t 10 --rooms 4
```

每个 worker：连接 → `REGISTER` → `JOIN` 房间 → 反复 `CHAT` 并等待自己的回显 → `QUIT`，
统计吞吐与端到端时延（脚本会复用压测账号，重复运行会自动改用 `LOGIN`）。

实测（`benchmarks/results/gomoku-bench-20261018-153954.md`）：

| 并发连接 | 吞吐 (msg/s) | p50 (ms) | p95 (ms) | p99 (ms) | 错误 |
|---:|---:|---:|---:|---:|---:|
| 10 | **4916** | 1.66 | 2.97 | 3.83 | 0 |
| 50 | 1895 | 20.54 | 32.20 | 38.90 | 0 |
| 200 | 641 | 264.95 | 469.84 | 598.71 | 0 |

**结论**：0 错误，说明高并发下房间状态机与广播没有出现错乱。吞吐随连接数下降有两个明确原因：

1. **广播放大**：4 个房间各 50 人，一条 `CHAT` 要 fan-out 给 50 条连接，网络与 CPU 都被放大；
2. **压测工具本身受 Python GIL 限制**（每个 worker 一个线程），200 连接时驱动端已是瓶颈。

服务端线程模型没有成为瓶颈的证据是：同进程的 HTTP 端点在 500 并发下仍能保持 3 万 QPS。
若要测服务端极限，应把压测客户端换成 C++/epoll 或提高机器规格。

### 12.6 最大在线客户端数与客户端延迟

除了吞吐，还关心两个指标：**最多能有多少客户端同时在线**，以及**客户端看到的往返延迟**。
工具是 `benchmarks/scale_test.py`（只用 Python 标准库，selector 驱动，单进程可撑数万连接；
脚本会自己拉起服务端，并把服务端与自己的 `ulimit -n` 抬到 65535）。

#### 12.6.1 最大在线客户端数

```bash
cd /forDsh
# 增量加压：已建立的连接保持不动，每档只补新增的那部分
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000 12000
# 继续往上推（认证较慢，给足超时）
python3 benchmarks/scale_test.py max-online --levels 16000 20000 --timeout 900
```

脚本流程：建连 → 逐个 `REGISTER`（重名自动回退 `LOGIN`）→ 稳定 1 秒 →
读服务端 `/stats` 的 `online` 字段交叉校验 → 达标才继续下一档。
**每个连接都是完成认证的在线会话**，不是只握手不发数据的裸连接。

| 目标在线 | 新增连接 | 实际连上 | 新增认证成功 | `/stats` 在线 | 建连耗时(s) | 认证耗时(s) |
|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 1000 | 1000 | 1000 | 1000 | 0.03 | 3.95 |
| 2000 | 1000 | 2000 | 1000 | 2000 | 0.03 | 4.30 |
| 4000 | 2000 | 4000 | 2000 | 4000 | 0.06 | 10.02 |
| 8000 | 4000 | 8000 | 4000 | 8000 | 0.14 | 19.04 |
| 12000 | 4000 | 12000 | 4000 | 12000 | 0.11 | 17.16 |
| 16000 | 16000 | 16000 | 16000 | 16000 | 5.28 | 86.16 |
| 20000 | 4000 | 20000 | 4000 | 20000 | 12.76 | 15.20 |

**结论：20000 个客户端同时在线且全部认证成功（服务端 `/stats` 的 `online=20000`），
测到脚本设定的最高档仍未触顶。** 真正的约束是文件描述符上限（`ulimit -n`），
因此服务端与压测端都先调到 65535。

> 首次运行这些昵称还不存在，会走 `REGISTER` 插入，单次比 `LOGIN` 慢一倍左右；
> 之后重跑自动转 `LOGIN`，上表的认证耗时就是 `LOGIN` 路径的数字。

另一个值得注意的数字是**认证速度约 210-250 次/秒**：服务端只用一条 MySQL 连接
（`Db` 用 mutex 串行化），每次注册/登录都是一个独立事务，InnoDB 默认
`innodb_flush_log_at_trx_commit=1` 会 fsync，所以"上人速度"受数据库限制而非网络层。
小规模对局完全够用。

#### 12.6.2 客户端往返延迟

```bash
cd /forDsh
# PING -> PONG：纯 Reactor 收发路径（不碰数据库、不广播）
python3 benchmarks/scale_test.py latency -c 1    -t 10
python3 benchmarks/scale_test.py latency -c 10   -t 10
python3 benchmarks/scale_test.py latency -c 100  -t 10
python3 benchmarks/scale_test.py latency -c 500  -t 10
python3 benchmarks/scale_test.py latency -c 1000 -t 10

# CHAT 回显：包含房间广播 fan-out 的真实聊天延迟
python3 benchmarks/scale_test.py latency -c 50  -t 10 --payload chat --rooms 4
python3 benchmarks/scale_test.py latency -c 200 -t 10 --payload chat --rooms 4
```

闭环节奏：每个连接同时只发一个请求，收到应答再发下一个（所以延迟里不含排队等待，
反映的是"当前并发下一条消息的端到端耗时"）。

| 并发连接 | 往返样本 | 吞吐 (req/s) | 平均 (ms) | p50 (ms) | p95 (ms) | p99 (ms) |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 194908 | 19490.8 | 0.051 | 0.047 | 0.070 | 0.112 |
| 10 | 595039 | 59503.9 | 0.167 | 0.148 | 0.301 | 0.367 |
| 100 | 546318 | 54631.8 | 1.830 | 1.368 | 1.785 | 2.215 |
| 500 | 529125 | 52912.5 | 9.441 | 9.650 | 10.732 | 11.652 |
| 1000 | 446511 | 44651.1 | 22.366 | 21.541 | 26.160 | 39.928 |

聊天（CHAT 广播）往返延迟：

| 并发连接 | 往返样本 | 吞吐 (req/s) | 平均 (ms) | p50 (ms) | p95 (ms) | p99 (ms) |
|---:|---:|---:|---:|---:|---:|---:|
| 50 | 257906 | 25790.6 | 1.938 | 0.446 | 9.624 | 21.021 |
| 200 | 95740 | 9574.0 | 20.912 | 16.149 | 52.145 | 81.635 |

**结论**

1. 单连接 RTT **0.047 ms**（p50），说明 epoll + 文本协议解析在几十微秒量级；
2. 并发上升时延迟近似线性增长（100 / 500 / 1000 连接对应 p50 1.4 / 9.7 / 21.5 ms），
   而总吞吐稳定在 4.5-5.5 万 req/s，符合"每连接一个未完成请求"的闭环特征；
3. 聊天比 PING 慢，是因为一条消息要 fan-out 给同房间所有成员（50 连接时 p50 0.45 ms、
   p99 21 ms；200 连接时 p50 16 ms、p99 82 ms）；
4. c=100 的 PING 用例出现过一次 2427 ms 的离群点（首轮建连/系统调度抖动），
   其 p99 只有 2.2 ms，看尾部延迟应以 p99 为准。

完整结果文件：`benchmarks/results/scale-latency-*.md`。

### 12.7 结果文件

```
benchmarks/results/
├── webbench-<时间>.md / .csv      # webbench 并发矩阵汇总
├── raw-<时间>/webbench-c*.txt     # 每档 webbench 原始输出
├── gomoku-bench-<时间>.md         # 游戏协议并发压测
└── scale-latency-<时间>.md        # 最大在线客户端数 + 客户端往返延迟
```

---

## 13. 常见问题 FAQ

**Q1：为什么必须在 WSL 里跑，不能在 Windows 直接编译？**
muduo 依赖 epoll / timerfd / eventfd，`Thread.cc` 用 `linux/unistd.h`、`CurrentThread.cc` 用 `execinfo.h`，
是 Linux 专用实现。Windows 侧只作为源码存放位置，编译运行都在 WSL2 的 Ubuntu 里。

**Q2：`/forDsh` 是什么？必须吗？**
是 `scripts/setup_env.sh` 建立的一个软链，指向源码真实目录（`/mnt/d/coding/仿muduo网络库/forDsh`），
只是为了让路径更短、更符合文档里的写法。不用软链也可以，直接把下文命令里的 `/forDsh` 换成绝对路径。

**Q3：编译很慢？**
源码在 `/mnt/d`（Windows 盘，9p 文件系统）上，编译 IO 会比 WSL 原生 ext4 慢。
如果觉得慢，可以把工程复制到 WSL 家目录编译：

```bash
cp -r /forDsh `/forDsh-build && cd `/forDsh-build && ./scripts/build.sh
```

**Q4：启动报 `MySQL 连接失败`？**
先确认 `systemctl is-active mysql`；再确认账号密码与库名（默认 gomoku/gomoku123/gomoku）；
若用了外部库，注意 GEANT 权限（`GRANT ALL ON gomoku.* TO 'gomoku'@'%'`）和防火墙。

**Q5：webbench 报 "Cannot fork" 或大量失败？**
先 `ulimit -n 65535`；并确认用的是默认 HTTP/1.0（不要加 `-2`），URL 以 `/` 结尾。

**Q6：端口被占用？**
默认用 9527（游戏）与 8080（HTTP）。换端口：`./scripts/run_server.sh --port 9555 --http-port 8090`，
客户端相应加 `--port 9555`。

**Q7：为什么先广播 OVER 再写库？战绩要等一会儿才更新？**
为了不在房间锁里做阻塞的数据库 IO（否则一个慢事务会拖住所有房间）。
所以 `OVER` 到达时数据库可能还没写完，测试与客户端都应认为"战绩随后更新"（`INFO` 会再推一次）。

**Q8：同一昵称能开两个客户端吗？**
不能。在线表保证同一昵称只有一个活跃连接，第二个会收到 `ERR ALREADY_ONLINE`。

**Q9：对局中断线会怎样？**
断线方判负（`FORFEIT_LEFT`），对手获胜并正常落库；房间无人后自动销毁。

---

## 14. 已知取舍与限制

1. **密码明文存储**：按需求"暂时直接存入"，未做哈希/加盐；协议也未加密（本机/局域网演示用）。
2. **数据库访问是同步阻塞的**：调用发生在 IO 线程回调里（注册/登录/结束一局），
   没有做 DB 线程池或异步化；由于访问频率低、都是小事务，实测不影响对局。
3. **战绩写库在 OVER 之后**：见 FAQ Q7。
4. **五子棋规则为休闲简化版**：黑先、任意方向 ≥5 连即胜、**不实现禁手**（长连/三三/四四禁手），
   满盘无五连为和棋。
5. **单房间上限 64 人**，房间号仅字母数字，昵称仅 ASCII。
6. **客户端输入是行式的**：异步消息到达时会清掉当前输入行再重绘提示符，
   极短时间内可能出现输入行与消息交错（终端行模式应用的常见限制）。
7. **压测数值受 WSL2 与 9p 文件系统影响**，结论只做同一环境下的相对比较。
8. **muduo 为逐字复制上游实现**，未做任何优化/裁剪（除构建脚本外）。

---

## 15. 许可证与致谢

- 本项目代码用于学习与演示。
- `muduo/` 为 [chenshuo/muduo](https://github.com/chenshuo/muduo) 核心源码副本，遵循其 **BSD 3-Clause** 许可证（见 `muduo/License`），版权归 Shuo Chen 所有。
- `tools/webbench/` 为 [EZLippi/WebBench](https://github.com/EZLippi/WebBench) 副本，遵循 **GPL v3**（见 `tools/webbench/LICENSE`），原作者 Radim Kolar。
- 感谢陈硕的《Linux 多线程服务端编程》与 muduo 项目，本项目的 Reactor 使用方式、线程模型与大量细节均来自它。
