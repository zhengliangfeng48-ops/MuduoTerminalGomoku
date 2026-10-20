# 压测方法说明

三套压测，结果统一落在 `results/`；实测数字摘要见 [../README.md](../README.md) 第 10 节。

| 工具 | 压什么 | 说明 |
|---|---|---|
| `run_webbench.sh` + `../tools/webbench/` | 服务端内置 HTTP 端点（8080） | 真实压 muduo 的 accept / epoll / 读写 / 关闭路径，QPS 以此为准 |
| `gomoku_bench.py` | 游戏文本协议（9527） | 注册/进房/CHAT 广播往返，验证高并发下房间状态机正确性 |
| `scale_test.py` | 连接规模 | `max-online` 量最大在线客户端数；`latency` 量客户端往返延迟 |

## 1. webbench

```bash
cd /forDsh
./scripts/build.sh                 # 编译服务端
make -C tools/webbench             # 编译 webbench
./benchmarks/run_webbench.sh       # 并发矩阵
```

脚本行为：端口空闲则临时拉起服务端（日志写 `/tmp`）跑完杀掉，否则复用已有实例 → `ulimit -n 65535` →
逐档 `webbench -c N -t DURATION http://127.0.0.1:8080/` → 解析 `Speed=... pages/min`（÷60 得 QPS）
与 `Requests: N susceed, N failed` → 生成 `results/webbench-<时间>.{md,csv}` 与 `results/raw-<时间>/`。

| 变量 | 默认 | 含义 |
|---|---|---|
| `LEVELS` | `1 10 50 100 200 500` | 并发档位 |
| `DURATION` | `10` | 每档秒数 |
| `HOST` / `GAME_PORT` / `HTTP_PORT` | 127.0.0.1 / 9527 / 8080 | 目标地址 |
| `IO_THREADS` | 4 | 临时启动服务端时的 IO 线程数 |
| `NO_START` | 0 | 1 = 不启动服务端，直接压已有实例 |

两个坑：URL 必须以 `/` 结尾；必须用默认 HTTP/1.0（别加 `-2`，服务端应答后主动关连接）。

```bash
ulimit -n 65535
./tools/webbench/webbench -c 100 -t 10 http://127.0.0.1:8080/          # 手动单次
./tools/webbench/webbench -c 200 -t 10 http://127.0.0.1:8080/stats    # 带 DB 查询的端点
```

## 2. 游戏协议压测

```bash
ulimit -n 65535
python3 benchmarks/gomoku_bench.py -c 50 -t 10 --rooms 4
```

| 参数 | 默认 | 含义 |
|---|---|---|
| `-c/--clients` | 50 | 并发连接数 |
| `-t/--time` | 10 | 压测秒数 |
| `--rooms` | 4 | 房间数（worker 均匀分布） |
| `--prefix` | `bench_` | 账号前缀（会写入 MySQL） |
| `--think-time` | 0 | 每条消息后的间隔 |

每个 worker：`REGISTER`（已存在则转 `LOGIN`）→ `JOIN` → 反复 `CHAT` 等回显 → `QUIT`。
该脚本用 Python 线程驱动、受 GIL 限制，**绝对值偏保守**，价值在"高并发下协议/房间逻辑是否出错"。

## 3. 最大在线客户端数与往返延迟

`scale_test.py` 只用标准库 + selector，单进程可撑数万连接；脚本自己拉起服务端，
并把服务端与压测进程的 `ulimit -n` 都提到 65535。
**全局参数（`--host/--port/--http-port/--no-start`）必须写在子命令前面。**

```bash
# 增量加压：已有连接保持不动，每档只补新增部分
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000 12000
python3 benchmarks/scale_test.py max-online --levels 16000 20000 --timeout 900

# PING -> PONG：纯 Reactor 收发路径
python3 benchmarks/scale_test.py latency -c 1 -t 10
python3 benchmarks/scale_test.py latency -c 100 -t 10
python3 benchmarks/scale_test.py latency -c 1000 -t 10
# CHAT 回显：包含房间广播 fan-out
python3 benchmarks/scale_test.py latency -c 50 -t 10 --payload chat --rooms 4
```

| 参数 | 子命令 | 默认 | 含义 |
|---|---|---|---|
| `--levels` | max-online | 100 500 1000 2000 4000 8000 | 逐档目标在线数 |
| `--hold` | max-online | 1.0 | 每档稳定观察时间（秒），之后查 `/stats` 校验 |
| `--timeout` | max-online | 120 | 每档认证超时（秒） |
| `--prefix` | 两者 | `bench_`/`scale_` | 账号前缀（会写入 MySQL，可复用） |
| `-c/--clients`、`-t/--duration` | latency | 100、10 | 并发连接数、压测秒数 |
| `--payload`、`--rooms` | latency | `ping`、4 | `ping` 纯收发 / `chat` 广播回显；chat 房间数 |

`scale_test.py` 只把报告打到 stdout、不写文件，留档要用重定向：
`... | tee results/scale-latency-<tag>-$(date +%Y%m%d-%H%M%S).md`。

## 4. 怎么解读

- **失败数必须为 0**：先看 `results/raw-*/webbench-c*.txt` 与服务端日志；
- **QPS 平台化**：HTTP 端点是单 IO 线程 + 每请求短连接，并发到一定程度就稳定，属预期；
- **游戏协议吞吐随并发下降**：房间广播是 O(房间人数)，每房间 50 人时一条消息复制 50 份；
- **延迟看 p99 不看 max**：首轮建连、系统调度会拉出离群点；
- **认证是上人瓶颈**：约 210–250 次/秒（单条 MySQL 连接 + 每事务 fsync），涨到 2 万在线要 60–90 秒；
- 数字只在**同环境**内做相对比较。
