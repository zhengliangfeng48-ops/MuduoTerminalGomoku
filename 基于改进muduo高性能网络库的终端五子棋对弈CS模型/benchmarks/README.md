# 压测方法说明

本目录提供两套压测：

| 工具 | 压什么 | 说明 |
|---|---|---|
| `run_webbench.sh` + `../tools/webbench/` | 服务端内置 HTTP 端点（8080） | 真实压 muduo 的 accept / epoll / 读写 / 关闭路径，QPS 数据以此为准 |
| `gomoku_bench.py` | 游戏文本协议（9527） | 压注册/进房/CHAT 广播往返，验证高并发下房间状态机正确性 |

结果统一落在 `results/`。

## 0. 本目录（自研版）与参考实现的差异

这份 `benchmarks/` 是从 `forDsh/benchmarks/` 复制来的，多了三处便利改动，其余用法完全一致：

| 改动 | 用法 |
|---|---|
| `run_webbench.sh` 支持 `TAG` | `TAG=opt1 ./benchmarks/run_webbench.sh` → 结果存为 `results/webbench-opt1-<时间>.md`，并在 Markdown 头部记录 tag 与二进制指纹（大小 + mtime） |
| 两个脚本都支持 `SERVER_BIN` | `SERVER_BIN=build-ref/bin/gomoku_server ...` 可让压测跑**参考实现**的二进制，用于 A/B 对照 |
| 多了 `RESULTS.md` | 优化前后的对比台账；每轮优化填一行，别把数字只留在终端里 |

`scale_test.py` 只把报告打印到 stdout（不写文件），所以要留档就用重定向：

```bash
python3 benchmarks/scale_test.py latency -c 100 -t 10 | tee benchmarks/results/scale-latency-opt1-$(date +%Y%m%d-%H%M%S).md
```

> **参数顺序有坑**：`--host/--port/--http-port/--no-start` 是**全局参数，必须写在子命令前面**。
> 写错顺序会报 `unrecognized arguments`：
>
> ```bash
> python3 benchmarks/scale_test.py --port 9627 --http-port 8180 max-online --levels 50   # 对
> python3 benchmarks/scale_test.py max-online --levels 50 --port 9627                    # 错
> ```

> 下文命令里的 `cd <自研目录>` 指本工程根目录；在参考实现里跑时换成 `cd forDsh`（或软链 `/forDsh`）。

---

## 1. webbench

### 1.1 准备

```bash
cd <自研目录>
./scripts/build.sh                 # 编译服务端
make -C tools/webbench             # 编译 webbench
```

### 1.2 一键跑并发矩阵

```bash
./benchmarks/run_webbench.sh
```

脚本行为：

1. 若 9527 端口空闲则临时拉起服务端（日志写 `/tmp/gomoku-bench-<时间>`），跑完自动杀掉；
   若已有服务在跑则直接复用；
2. `ulimit -n 65535`；
3. 对每个并发档位执行 `webbench -c N -t DURATION http://127.0.0.1:8080/`；
4. 从 webbench 输出解析：

   ```
   Speed=2055480 pages/min, 12812418 bytes/sec.
   Requests: 171290 susceed, 0 failed.
   ```

   → QPS = pages/min ÷ 60，传输速率 = bytes/sec ÷ 1048576；
5. 生成 `results/webbench-<时间>.{md,csv}` 与 `results/raw-<时间>/`。

### 1.3 可调参数（环境变量）

| 变量 | 默认 | 含义 |
|---|---|---|
| `LEVELS` | `1 10 50 100 200 500` | 并发档位 |
| `DURATION` | `10` | 每档秒数 |
| `HOST` / `GAME_PORT` / `HTTP_PORT` | 127.0.0.1 / 9527 / 8080 | 目标地址 |
| `IO_THREADS` | 4 | 临时启动服务端时的 IO 线程数 |
| `NO_START` | 0 | 1 = 不启动服务端，直接压已有实例 |
| `WB` | tools/webbench/webbench | webbench 可执行文件路径 |

例子：

```bash
DURATION=5 LEVELS="100 500 1000" IO_THREADS=8 ./benchmarks/run_webbench.sh
NO_START=1 ./benchmarks/run_webbench.sh          # 先自己起好服务端再压
```

### 1.4 手动单次

```bash
ulimit -n 65535
./tools/webbench/webbench -c 100 -t 10 http://127.0.0.1:8080/
./tools/webbench/webbench -c 200 -t 10 http://127.0.0.1:8080/stats    # 带 DB 查询的端点
```

> 两个坑：URL 必须以 `/` 结尾；必须用 HTTP/1.0（默认，别加 `-2`）。

---

## 2. 游戏协议压测

```bash
cd <自研目录>
ulimit -n 65535
python3 benchmarks/gomoku_bench.py -c 50 -t 10 --rooms 4
```

| 参数 | 默认 | 含义 |
|---|---|---|
| `-c/--clients` | 50 | 并发连接数 |
| `-t/--time` | 10 | 压测秒数 |
| `--rooms` | 4 | 房间数（worker 均匀分布） |
| `--prefix` | `bench_` | 压测账号前缀（会写入 MySQL） |
| `--think-time` | 0 | 每条消息后的间隔 |

每个 worker：`REGISTER`（已存在则自动转 `LOGIN`）→ `JOIN` → 反复 `CHAT` 等回显 → `QUIT`；
输出吞吐与 p50/p95/p99 端到端时延。

> 该脚本用 Python 线程驱动，受 GIL 限制，**绝对值偏保守**；
> 它的价值在于"高并发下协议/房间逻辑是否出错（错误数）"。
> 想测服务端极限请用 C++/epoll 客户端或直接看 webbench 的 HTTP 数据。

---

## 3. 最大在线客户端数与客户端延迟

工具：`scale_test.py`（Python 标准库，selector 驱动，单进程可撑数万连接；
脚本自己拉起服务端，并把服务端与压测进程的 `ulimit -n` 都提到 65535）。

### 3.1 最大在线客户端数

```bash
cd <自研目录>
# 增量加压：已建立的连接保持不动，每档只补新增的那部分
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000 12000
# 继续往上推（认证受数据库限制，给足超时）
python3 benchmarks/scale_test.py max-online --levels 16000 20000 --timeout 900
```

| 参数 | 默认 | 含义 |
|---|---|---|
| `--levels` | 100 500 1000 2000 4000 8000 | 逐档目标在线数 |
| `--hold` | 1.0 | 每档稳定观察时间（秒），之后查 `/stats` 校验 |
| `--timeout` | 120 | 每档认证超时（秒） |
| `--prefix` | `scale_` | 账号前缀（会写入 MySQL，可复用） |
| `--no-start` | 关 | 复用已在运行的服务端 |

实测（`results/scale-latency-*.md`）：**20000 个客户端同时在线且全部认证成功**，
`/stats` 的 `online` 字段等于连接数，测到最高档仍未触顶。
瓶颈不是网络层，而是：
- 文件描述符上限（`ulimit -n`，服务端与压测端都要 65535）；
- 认证吞吐约 210-250 次/秒（单条 MySQL 连接 + 每事务 fsync），所以"涨到 2 万在线"要 60-90 秒。
  首次运行账号不存在，会走 `REGISTER` 插入，认证耗时约为表里的两倍。

### 3.2 客户端往返延迟

```bash
cd <自研目录>
# PING -> PONG：纯 Reactor 收发路径
python3 benchmarks/scale_test.py latency -c 1    -t 10
python3 benchmarks/scale_test.py latency -c 100  -t 10
python3 benchmarks/scale_test.py latency -c 1000 -t 10
# CHAT 回显：包含房间广播 fan-out
python3 benchmarks/scale_test.py latency -c 50  -t 10 --payload chat --rooms 4
python3 benchmarks/scale_test.py latency -c 200 -t 10 --payload chat --rooms 4
```

| 参数 | 默认 | 含义 |
|---|---|---|
| `-c/--clients` | 100 | 并发连接数 |
| `-t/--duration` | 10 | 压测秒数 |
| `--payload` | `ping` | `ping`=纯收发；`chat`=聊天广播回显 |
| `--rooms` | 4 | chat 模式下的房间数 |

闭环节奏（一个请求未返回不发下一个），实测：

| 并发 | PING p50 | PING p99 | CHAT p50 | CHAT p99 |
|---:|---:|---:|---:|---:|
| 1 | 0.047 ms | 0.112 ms | – | – |
| 10 | 0.148 ms | 0.367 ms | – | – |
| 50 | – | – | 0.446 ms | 21.0 ms |
| 100 | 1.368 ms | 2.215 ms | – | – |
| 200 | – | – | 16.1 ms | 81.6 ms |
| 500 | 9.650 ms | 11.652 ms | – | – |
| 1000 | 21.541 ms | 39.928 ms | – | – |

---

## 4. 如何解读结果

- **失败数必须为 0**：出现失败先看 `results/raw-*/webbench-c*.txt` 与服务端日志；
- **QPS 平台化**：本项目 HTTP 端点是单 IO 线程、且每个请求都短连接，
  所以并发涨到一定程度 QPS 会稳定（瓶颈是建连/关闭与单线程处理），这是预期行为；
- **游戏协议吞吐随并发下降**：房间内广播是 O(房间人数)，
  每房间 50 人时一条消息要复制 50 份，属正常放大；
- **延迟看 p99 而不是最大值**：偶发离群点（首轮建连、系统调度）会拉高 max；
- 所有压测都在 WSL2 上完成，数值只用于**同环境相对比较**。
