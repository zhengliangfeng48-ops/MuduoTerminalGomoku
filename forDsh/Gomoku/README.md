# Gomoku 组件说明

本目录是基于 `../muduo` 的终端五子棋服务端 + 客户端实现。
**所有源码都有中文注释**，复杂处（Reactor 调用链、跨线程状态、协议状态机、胜负判定、MySQL 事务）重点标注。

总览与使用说明见 [../README.md](../README.md)。

---

## 1. 模块与职责

| 文件 | 职责 | 关键点 |
|---|---|---|
| `common/Protocol.{h,cc}` | 协议常量、错误码、昵称/密码/房号校验、坐标解析、胜率格式化 | 服务端与客户端共用，零依赖 |
| `server/main.cc` | 入口：参数 → 日志 → EventLoop → ServerApp | 忽略 `SIGPIPE`；`AsyncLogging` 用函数指针跳板 |
| `server/Config.{h,cc}` | 命令行 + `GOMOKU_*` 环境变量 | 手写解析器，避免引入 boost::program_options |
| `server/ServerApp.{h,cc}` | 装配 TcpServer、分发命令、在线表、统计、写库与推送战绩 | HTTP 服务的构造与启动必须投递到它自己的 loop |
| `server/Session.{h,cc}` | 每连接状态机 | 只持连接 `weak_ptr`；`send()` 可跨线程 |
| `server/RoomManager.{h,cc}` | 房间表 + 全局互斥 | 房间操作纯内存，持锁极短 |
| `server/Room.{h,cc}` | 座位、聊天可见性、对局生命周期、广播 | 方法假定调用方已持锁（约定写在头注释里） |
| `server/GomokuGame.{h,cc}` | 15×15 棋盘与胜负判定 | 纯逻辑、无网络/DB，便于单测 |
| `server/AuthService.{h,cc}` | 注册/登录/战绩事务 | 昵称唯一 + 单事务更新双方战绩 |
| `server/Db.{h,cc}` | MySQL C API 封装 | mutex 串行化、`mysql_ping` 重连、集中转义 |
| `server/HttpStatusServer.{h,cc}` | 压测端点 | 独立 EventLoopThread，单 IO 线程 |
| `client/main.cc` | 登录引导（密码关回显）+ 事件循环 | 支持 `--nick/--password/--register` 脚本化 |
| `client/GameClient.{h,cc}` | TcpClient 封装、消息分发、本地命令 | 支持 `H8` 与 `8 8` 两种落子输入 |
| `client/TerminalUI.{h,cc}` | 棋盘/房间头/聊天渲染 | ANSI，`--no-color` 可关 |
| `client/StdinLoop.{h,cc}` | stdin 独立线程 | `runInLoop` 投递回 IO 线程 |

---

## 2. 关键设计说明

### 2.1 为什么 Session 只持有连接的 weak_ptr

`TcpConnection::setContext(session)` 让**连接持有 Session 的 shared_ptr**；
如果 Session 反过来持有连接的 shared_ptr，就形成环，连接永远释放不掉。
因此 Session 存 `weak_ptr<TcpConnection>`，发送时 `lock()` 一下即可。
（cyclic reference 是 muduo 使用者最常见的内存泄漏来源。）

### 2.2 哪些状态需要加锁

| 共享状态 | 保护方式 | 原因 |
|---|---|---|
| `RoomManager::rooms_` 与所有 `Room` 内部状态 | `RoomManager::mutex_` | 不同连接可能在**不同 IO 线程**上操作同一个房间 |
| `AuthService` / `Db` | 各自 mutex | MySQL `MYSQL*` 不是线程安全的 |
| `online_` 在线表、统计计数 | mutex + `std::atomic` | 多个 IO 线程 + HTTP 线程都会读 |

不需要加锁的：`Session` 的 `authed_/nickname_/roomId_` —— 只有它所属的 IO 线程会读写。

### 2.3 广播为什么是线程安全的

`Room::broadcast()` 遍历成员的 weak_ptr，逐个调用 `Session::send()` →
`TcpConnection::send()`。muduo 内部会判断"当前线程是不是该连接的 loop 线程"，
不是就 `runInLoop` 排队并唤醒目标 loop。因此从任意线程广播都是安全的。

### 2.4 为什么先广播 OVER、后写数据库

如果先写库，写库必须发生在 `RoomManager` 的锁内（因为结算是在房间操作中触发的），
一个慢 SQL 就会拖住所有房间。所以：

```
Room::finishGame()  ──► 广播 BOARD / OVER（纯内存）
        │
        └─► GameOverInfo ──► ServerApp::onGameOver()
                                   ├─ AuthService::recordResult()  （锁外，事务）
                                   └─ 给双方推 INFO <games> <wins> <rate>
```

代价是"OVER 到达时数据库可能还没写完"，所以 `tests/e2e_test.py` 用轮询校验战绩。

### 2.5 房间/对局状态机

```
        JOIN                SIT(第 1 人)             SIT(第 2 人)
  ─────────────► IDLE ──────────────────► IDLE ───────────────────► PLAYING
                 │                        │                            │
                 │ LEAVE                  │ STAND                      │ 五连/满盘/认输/断线
                 ▼                        ▼                            ▼
              (房间销毁)              IDLE                        WAITREADY
                                                                       │
                                              双方都 READY             │
                                          ───────────────────────────► PLAYING
```

- `PLAYING` 期间：第三人 `SIT` → `ERR GAME_RUNNING`；观战者 `CHAT` → `ERR CHAT_LOCKED`；
- `WAITREADY` 期间：所有人可聊天，双方 `READY` 后开新局；
- 对局中 `STAND`/`LEAVE`/断线 → 对手胜（原因分别 `FORFEIT_STAND` / `FORFEIT_LEFT`）。

### 2.6 胜负判定

对**最后一手**沿 4 条轴（横、竖、主斜、副斜）双向累加同色棋子，任一方向 ≥5 即胜；
落子后若 `moves()==225` 且无人五连 → 和棋。复杂度 O(1)（每条轴最多数 8 格）。

### 2.7 MySQL 的两个坑（已修）

- 整数除法：`wins / games` 在 MySQL 里是整数除法，必须写 `games + 1.0`；
- `SET` 从左到右求值：`win_rate` 必须写在 `games` / `wins` 之前，否则用到的是更新后的值。

---

## 3. 目录内测试

```bash
./build/bin/gomoku_board_test          # 棋盘/协议单测（591 断言）
python3 Gomoku/tests/e2e_test.py       # 协议级端到端（38 断言，自动起临时服务端）
```

---

## 4. 想继续扩展时

- 加规则（禁手/AI）：只改 `GomokuGame`，网络层不用动（它是纯逻辑类）；
- 加功能（观战聊天室、排行榜）：在 `Room` / `AuthService` 上加，注意写库仍在锁外；
- 提升并发：把 `Db` 换成"专用 DB 线程 + 任务队列"或连接池；
- 提升压测 QPS：让 `HttpStatusServer` 也 `setThreadNum(N)`，或支持 HTTP keep-alive。
