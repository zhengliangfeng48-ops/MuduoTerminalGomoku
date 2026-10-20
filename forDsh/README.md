# Gomoku 终端五子棋（基于 muduo 网络库）

> 纯终端文字界面的五子棋联机对弈：服务端基于陈硕 muduo 的 Reactor 网络库实现，客户端是同仓库的终端程序；
> 账号与战绩（场次 / 胜率）持久化到 MySQL，并附 **webbench** 压力/性能测试与实测数据。

子目录各有一份专门文档：[muduo/](muduo/README.md)、[Gomoku/](Gomoku/README.md)、[benchmarks/](benchmarks/README.md)。

---

## 1. 功能一览

| 需求 | 实现 |
|---|---|
| 输入房间号进入房间 | `JOIN <房号>`，房间不存在则**自动创建**（房号 1–16 位字母数字） |
| 房间无人对弈即可上座 | `SIT` 占第一个空座（先黑后白）；两座坐满**立即开局** |
| 非对弈期全房可聊天 | 未开局时房间内所有人（含观战者）都可 `CHAT` |
| 对弈期仅双方可聊 | 观战者 `CHAT` 返回 `ERR CHAT_LOCKED` |
| 启动需要注册 | 客户端启动先注册/登录，没有账号必须先注册 |
| 昵称不能重复 | 服务端先查重，DB 上还有 `UNIQUE KEY uk_nickname` 兜底 |
| 密码暂时直接存入 | `users.password` 明文存储（按需求） |
| 场次 / 胜率入库 | 每局结束在**一个事务**里更新双方 `games/wins/win_rate` 并写 `match_history` |
| 纯终端画面 | 15×15 文字棋盘 + 房间头 + 聊天/系统消息，ANSI 颜色可关 |
| 压力 / 性能测试 | 内置 webbench 压 HTTP 端点 + Python 压游戏协议 |

附加：`READY` 再来一局、起身/断线判负、`ROOMS`/`WHO`/`STATS`、`/health` 与 `/stats`。

---

## 2. 架构

```
 gomoku_client（终端，TcpClient + stdin 线程）
        │  TCP 文本行协议（\n 分帧）
        ▼
 gomoku_server
  TcpServer(9527) ─ EventLoopThreadPool ─ Session / RoomManager / Room / GomokuGame
  TcpServer(8080) ─ 独立 EventLoopThread ─ 极简 HTTP：/  /health  /stats
  AuthService ─ Db（MySQL C API，事务）
        │
        ▼  MySQL 8：users / match_history

 webbench ──► http://127.0.0.1:8080/   压真实的 accept/epoll/读写/关闭路径
```

线程模型：`main` 跑 base loop 负责 accept；`setThreadNum(N)` 起 N 个 IO 线程，
**每条连接的回调只在它自己的 IO 线程执行**；跨线程共享的只有三处（房间表、Auth/Db、在线表），
各有锁；广播走 `TcpConnection::send()`，muduo 内部负责排队并唤醒目标 loop；
HTTP 端点单独一个线程，压测流量不干扰对局。

---

## 3. 目录

```
forDsh/
├── muduo/            muduo 核心库（上游逐字副本）+ 3 份 CMake + README
├── Gomoku/
│   ├── common/       Protocol（常量、错误码、校验、坐标解析）
│   ├── server/       main Config ServerApp Session RoomManager Room
│   │                 GomokuGame AuthService Db HttpStatusServer
│   ├── client/       main GameClient TerminalUI StdinLoop
│   ├── sql/          schema.sql
│   └── tests/        board_test.cc（591 项）、e2e_test.py（38 项）
├── scripts/          setup_env build init_db run_server run_client
├── benchmarks/       run_webbench.sh gomoku_bench.py scale_test.py results/
└── tools/webbench/   webbench 1.5（一处可移植性补丁）
```

构建产物：`build/lib/libmuduo_base.a`、`libmuduo_net.a`、`build/bin/{gomoku_server,gomoku_client,gomoku_board_test}`。

---

## 4. 环境与依赖

只在 **Linux** 上编译运行（依赖 epoll / timerfd / eventfd、`execinfo.h`、`linux/unistd.h`，不做 Windows 原生移植）。

| 依赖 | 用途 |
|---|---|
| g++ ≥ 7、cmake ≥ 3.10、make | 编译 |
| libboost-dev | muduo 用 `boost::any` / `boost::circular_buffer` / `boost::operators`（只需头文件） |
| libmysqlclient-dev | MySQL C API |
| mysql-server | 本地数据库 |
| python3 | 端到端测试 + 协议压测 |

`./scripts/setup_env.sh` 会装依赖并建立 `/forDsh` 软链（下文命令统一用它）。
本机验证过的版本：g++ 11.4、CMake 3.22、MySQL 8.0。

---

## 5. 数据库

```bash
./scripts/init_db.sh      # 启库 + 建表 + 建账号 + 自检，等价于 mysql < Gomoku/sql/schema.sql
```

默认库 `gomoku`、账号 `gomoku / gomoku123`（生产环境改密码，同步到 `--db-pass` 或 `GOMOKU_DB_PASS`）。

- `users`：昵称全局唯一，`games / wins / win_rate`；
- `match_history`：房号、双方、结果（black/white/draw）、原因、手数。

战绩在一个事务里更新，两条写法不能改：

1. 必须写 `games + 1.0`，否则 MySQL 整数除法把胜率算成 0 或 1；
2. `SET` 从左到右求值，`win_rate` 必须写在 `games` / `wins` 之前，否则读到的是已 +1 的新值。

数据库不可用时服务端仍能启动，注册/登录返回 `ERR DB_UNAVAILABLE`，对局本身不受影响。

---

## 6. 编译与启动

```bash
./scripts/build.sh
./scripts/run_server.sh                                   # 游戏 9527 / HTTP 8080
./scripts/run_server.sh --port 9000 --http-port 8081 --io-threads 8
```

| 参数 | 默认 | 说明 |
|---|---|---|
| `--listen` | `0.0.0.0` | 监听地址 |
| `--port` | `9527` | 游戏协议端口 |
| `--http-port` | `8080` | 压测 HTTP 端口 |
| `--io-threads` | `4` | IO 线程数；`0` = 全跑在 base loop |
| `--log-dir` | `logs` | AsyncLogging 滚动日志目录（空串 = 只输出到终端） |
| `--db-host/--db-port/--db-user/--db-pass/--db-name` | 本机 / 3306 / gomoku / gomoku123 / gomoku | MySQL |

同名环境变量 `GOMOKU_*` 可用，命令行优先。启动自检：

```bash
curl -s http://127.0.0.1:8080/health    # {"status":"ok"}
curl -s http://127.0.0.1:8080/stats     # 连接数/在线/房间/对局/uptime/DB 状态
```

---

## 7. 客户端

```bash
./scripts/run_client.sh --nick alice --password alice123 --register
./scripts/run_client.sh --host 127.0.0.1 --port 9527 --nick bob --password bob123 --login
```

| 输入 | 作用 |
|---|---|
| `/join <房号>` | 进入房间（不存在自动创建） |
| `/leave` / `/sit` / `/stand` | 离开（对局中 = 认输）/ 上座 / 起身（对局中 = 认输） |
| `/ready` | 上一局结束后准备下一局（双方都 READY 才开新局） |
| `H8` 或 `8 8`，或 `/move <列> <行>` | 落子（列 A–O，行 1–15） |
| `/chat <文本>` | 聊天；对局中只有对弈双方可发 |
| `/board` `/who` `/rooms` `/stats` | 重绘 / 成员 / 房间列表 / 我的战绩 |
| `/clear` `/quit` `/help` | 清屏 / 退出 / 帮助 |
| 其它任意文本 | 等同 `/chat` |

---

## 8. 协议

TCP 上的 UTF-8 文本行协议，一行一条、以 `\n` 分帧（服务端自动剥 `\r`，可直接用 nc 调试）。
单行上限 1024 字节，聊天正文上限 512 字节。
昵称 2–16 位 `[A-Za-z0-9_]` 且首字符字母数字；密码 4–32 位可打印 ASCII 无空格；房号 1–16 位 `[0-9A-Za-z]`。

**客户端 → 服务端**

| 命令 | 说明 |
|---|---|
| `REGISTER <昵称> <密码>` / `LOGIN <昵称> <密码>` | 注册并登录 / 登录 |
| `JOIN <房号>` / `LEAVE` | 进房（自动建房）/ 离开 |
| `SIT` / `STAND` / `READY` | 上座 / 起身 / 准备下一局 |
| `MOVE <列> <行>` | 落子（1–15） |
| `CHAT <文本>` | 聊天 |
| `BOARD` / `WHO` / `ROOMS` / `STATS` | 查询 |
| `PING` / `QUIT` | 心跳 / 断开 |

**服务端 → 客户端**

| 消息 | 说明 |
|---|---|
| `OK <msg>` / `ERR <CODE> <msg>` | 通用应答 |
| `AUTH <昵称> <场次> <胜场> <胜率>` | 登录成功 + 战绩 |
| `ROOM <房号> <状态> <黑> <白> <自己>` | 房间快照（IDLE / WAITREADY / PLAYING） |
| `USERS <n1,n2,...>` / `JOINED <昵称>` / `LEFT <昵称>` | 成员与变化 |
| `SEATS <黑> <白> <状态>` | 座位变化（空位 `-`） |
| `START <黑> <白>` / `TURN <昵称>` | 开局 / 轮次 |
| `PLACED <列> <行> <昵称> <B\|W>` | 落子 |
| `BOARD <225 字符> <最后列> <最后行>` | 棋盘快照（`.`/`X`/`O`，行优先） |
| `OVER <WON\|DRAW> <胜者> <原因>` | 结束 |
| `INFO <场次> <胜场> <胜率>` | 战绩刷新 |
| `CHAT <昵称> <文本>` / `SYS <文本>` | 聊天 / 系统消息 |
| `ROOMS <房号:状态:黑:白:人数\|...>` / `PONG` | 房间列表 / 心跳应答 |

结束原因：`FIVE`（五连）、`FULL`（满盘和棋）、`FORFEIT_STAND`（起身认输）、`FORFEIT_LEFT`（断线判负）。

错误码：`NOT_LOGIN`、`BAD_CMD`、`BAD_ARG`、`NAME_TAKEN`、`NO_SUCH_USER`、`BAD_PASSWORD`、
`ALREADY_ONLINE`、`ALREADY_AUTHED`、`NOT_IN_ROOM`、`ALREADY_IN_ROOM`、`GAME_RUNNING`、
`SEAT_FULL`、`NOT_SEATED`、`NOT_YOUR_TURN`、`ILLEGAL_MOVE`、`CHAT_LOCKED`、
`DB_UNAVAILABLE`、`LINE_TOO_LONG`、`ROOM_FULL`。

---

## 9. 测试

```bash
./build/bin/gomoku_board_test        # 棋盘/协议单测，591 项断言，末行 PASS
python3 Gomoku/tests/e2e_test.py     # 协议级 e2e，38 checks / 0 failures
```

e2e 会自动拉起临时服务端（19527/18080，独立日志目录），用原始 socket 覆盖 11 组场景：
未登录被拒、注册与初始战绩、昵称唯一/重复在线/错误密码、进房自动建房与坐满开局、轮次与非法落子、
五连判胜 + 数据库校验、赛后 READY 重开与观战者限制、起身认输、断线判负、房间与成员查询、HTTP 端点。
战绩校验用**轮询**——服务端先广播 `OVER` 再写库（原因见 [Gomoku/README.md](Gomoku/README.md)）。

客户端冒烟：

```bash
{ sleep 1; echo "/join 9001"; sleep 0.5; echo "/sit"; sleep 0.5; echo "H8"; sleep 1; echo "/quit"; } \
  | ./build/bin/gomoku_client --nick smoke --password smoke123 --register --no-color
```

---

## 10. 性能测试

三套压测（webbench 压 HTTP、`gomoku_bench.py` 压游戏协议、`scale_test.py` 量最大在线与往返延迟）
的方法、参数与坑见 [benchmarks/README.md](benchmarks/README.md)，原始数据在 `benchmarks/results/`。

实测结论：

1. webbench 6 档并发 **0 失败**，峰值约 3.5 万 req/s，之后平台化——瓶颈是"每请求一次 connect/close +
   单线程 HTTP 端点"，不是连接数；
2. 游戏协议压测 0 错误，吞吐随并发下降来自广播放大（每房间 50 人 → 一条 CHAT 复制 50 份）与
   Python 压测端 GIL；
3. **20000 客户端同时在线且全部认证成功**，未触顶；真正的约束是 `ulimit -n`；
   上人速度受认证吞吐限制（约 210–250 次/秒，单条 MySQL 连接 + 每事务 fsync）；
4. 单连接 PING 往返 p50 **0.047 ms**；并发上升延迟近似线性增长，延迟看 **p99** 不看 max。

---

## 11. FAQ

**Q：为什么只能在 Linux 上编译运行？**
muduo 依赖 epoll / timerfd / eventfd，`Thread.cc` 用 `linux/unistd.h`、`CurrentThread.cc` 用 `execinfo.h`，
是 Linux 专用实现。

**Q：`/forDsh` 是什么？必须吗？**
`scripts/setup_env.sh` 建立的软链，只为缩短路径。不用也行，把命令里的 `/forDsh` 换成绝对路径。

**Q：启动报 MySQL 连接失败？**
先 `systemctl is-active mysql`；再核对账号/密码/库名；外部库还要 `GRANT ALL ON gomoku.* TO 'gomoku'@'%'`。

**Q：webbench 报 "Cannot fork" 或大量失败？**
`ulimit -n 65535`；确认用默认 HTTP/1.0（别加 `-2`）、URL 以 `/` 结尾。

**Q：端口被占用？**
默认 9527 / 8080，先 `ss -lntp | grep 9527`，或换端口（客户端同步加 `--port`）。

**Q：为什么收到 `OVER` 后战绩还没更新？**
为了不在房间锁里做阻塞的数据库 IO，服务端先广播后写库，`INFO` 会再推一次；客户端/测试按"随后更新"处理。

**Q：同一昵称能开两个客户端吗？** 不能，第二个收到 `ERR ALREADY_ONLINE`。

**Q：对局中断线会怎样？** 断线方判负（`FORFEIT_LEFT`），对手获胜并落库；房间无人后销毁。

---

## 12. 已知取舍

1. 密码明文存储、协议不加密：按需求，仅用于本机/局域网演示。
2. 数据库访问同步阻塞在 IO 线程回调里（注册/登录/结算），没有连接池或异步化；频率低、都是小事务。
3. 战绩写库在 `OVER` 之后，见 FAQ。
4. 规则为休闲简化版：黑先、任意方向 ≥5 连即胜、**不实现禁手**、满盘和棋。
5. 单房间上限 64 人，房间号仅字母数字，昵称仅 ASCII。
6. 客户端是行式输入，异步消息到达会重绘提示符，极短时间可能出现输入行与消息交错。
7. `muduo/` 为上游逐字副本，未做任何优化或裁剪。

---

## 13. 许可与致谢

- `muduo/` 为 [chenshuo/muduo](https://github.com/chenshuo/muduo) 核心源码副本，**BSD 3-Clause**（见 `muduo/License`），版权归 Shuo Chen。
- `tools/webbench/` 为 [EZLippi/WebBench](https://github.com/EZLippi/WebBench) 副本，**GPL v3**，原作者 Radim Kolar。
- 感谢陈硕的《Linux 多线程服务端编程》与 muduo 项目：本项目的 Reactor 用法、线程模型与大量细节都来自它。
