# Gomoku 组件说明

基于 `../muduo` 的终端五子棋服务端 + 客户端，源码全中文注释（Reactor 调用链、跨线程状态、
协议状态机、胜负判定、MySQL 事务处重点标注）。使用方法与协议见 [../README.md](../README.md)。

## 1. 模块与职责

| 文件 | 职责 | 关键点 |
|---|---|---|
| `common/Protocol.{h,cc}` | 协议常量、错误码、昵称/密码/房号校验、坐标解析、胜率格式化 | 服务端与客户端共用，零依赖 |
| `server/main.cc` | 参数 → 日志 → EventLoop → ServerApp | 忽略 `SIGPIPE`；`AsyncLogging` 用函数指针跳板 |
| `server/Config.{h,cc}` | 命令行 + `GOMOKU_*` 环境变量 | 手写解析，不引入 boost::program_options |
| `server/ServerApp.{h,cc}` | 装配 TcpServer、命令分发、在线表、统计、写库与推战绩 | HTTP 服务的构造与启动必须投递到它自己的 loop |
| `server/Session.{h,cc}` | 每连接状态机 | 只持连接 `weak_ptr`；`send()` 可跨线程 |
| `server/RoomManager.{h,cc}` | 房间表 + 全局互斥 | 房间操作纯内存，持锁极短 |
| `server/Room.{h,cc}` | 座位、聊天可见性、对局生命周期、广播 | 方法假定调用方已持锁（头注释里写明） |
| `server/GomokuGame.{h,cc}` | 15×15 棋盘与胜负判定 | 纯逻辑，无网络/DB，便于单测 |
| `server/AuthService.{h,cc}` | 注册/登录/战绩事务 | 昵称唯一 + 单事务更新双方战绩 |
| `server/Db.{h,cc}` | MySQL C API 封装 | mutex 串行化、`mysql_ping` 重连、集中转义 |
| `server/HttpStatusServer.{h,cc}` | 压测端点 | 独立 EventLoopThread，单 IO 线程 |
| `client/main.cc` | 登录引导（密码关回显）+ 事件循环 | 支持 `--nick/--password/--register` 脚本化 |
| `client/GameClient.{h,cc}` | TcpClient 封装、消息分发、本地命令 | 支持 `H8` 与 `8 8` 两种落子输入 |
| `client/TerminalUI.{h,cc}` | 棋盘/房间头/聊天渲染 | ANSI，`--no-color` 可关 |
| `client/StdinLoop.{h,cc}` | stdin 独立线程 | `runInLoop` 投递回 IO 线程 |

## 2. 锁边界

| 共享状态 | 保护方式 | 原因 |
|---|---|---|
| `RoomManager::rooms_` 与所有 `Room` 内部状态 | `RoomManager::mutex_` | 不同连接可能在**不同 IO 线程**上操作同一个房间 |
| `AuthService` / `Db` | 各自 mutex | MySQL `MYSQL*` 不是线程安全的 |
| `online_` 在线表、统计计数 | mutex + `std::atomic` | 多个 IO 线程 + HTTP 线程都会读 |

不需要加锁：`Session` 的 `authed_/nickname_/roomId_`——只有它所属的 IO 线程读写。

## 3. 为什么 Session 只持连接的 weak_ptr

`TcpConnection::setContext(session)` 让**连接持有 Session 的 shared_ptr**；若 Session 反向强持有连接，
就成环、连接永远释放不掉。所以 Session 存 `weak_ptr<TcpConnection>`，发送时 `lock()`。
（cyclic reference 是 muduo 使用者最常见的内存泄漏来源。）

广播线程安全的原因：`Room::broadcast()` 逐个调 `Session::send()` → `TcpConnection::send()`，
muduo 内部判断当前线程是不是该连接的 loop 线程，不是就 `runInLoop` 排队并唤醒目标 loop。

## 4. 先广播 OVER、后写数据库

写库若发生在 `RoomManager` 锁内，一个慢 SQL 会拖住所有房间。所以：

```
Room::finishGame()  ──► 广播 BOARD / OVER（纯内存）
        └─► GameOverInfo ──► ServerApp::onGameOver()
                                ├─ AuthService::recordResult()   锁外，事务
                                └─ 给双方推 INFO <games> <wins> <rate>
```

代价是"OVER 到达时库可能还没写完"，所以 e2e 用轮询校验战绩（`wait_stats`）。

## 5. 房间/对局状态机

```
        JOIN            SIT(第 1 人)          SIT(第 2 人)
  ──────────► IDLE ──────────────► IDLE ─────────────────► PLAYING
               │                    │                        │
               │ LEAVE              │ STAND                  │ 五连/满盘/认输/断线
               ▼                    ▼                        ▼
            (房间销毁)            IDLE                  WAITREADY
                                                            │ 双方 READY
                                                            └──► PLAYING
```

- `PLAYING` 期间：第三人 `SIT` → `ERR GAME_RUNNING`；观战者 `CHAT` → `ERR CHAT_LOCKED`；
- `WAITREADY` 期间所有人可聊天，双方 `READY` 后开新局；
- 对局中 `STAND`/`LEAVE`/断线 → 对手胜（`FORFEIT_STAND` / `FORFEIT_LEFT`）。

## 6. 胜负判定

对**最后一手**沿 4 条轴（横、竖、主斜、副斜）双向累加同色棋子，任一方向 ≥5 即胜；
落子后若 `moves()==225` 且无人五连 → 和棋。O(1)（每条轴最多数 8 格），无禁手。
