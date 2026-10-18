# 性能对比记录（优化前后）

这个文件是**唯一的性能对比台账**。每做一轮优化，就在下面表中追加一行，
并把原始结果留在 `results/` 里（文件名带同一个 `TAG`）。

## 怎么产生一行

```bash
# 1) 编译（同一份 Gomoku 代码，只是 muduo 换了版本）
./scripts/build.sh

# 2) 三项指标，逐项跑（口径必须一致，见下方"固定口径"）
TAG=<轮次> ./benchmarks/run_webbench.sh                       # QPS，文件名自带 tag
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000 12000 \
  | tee results/scale-online-<轮次>-$(date +%Y%m%d-%H%M%S).md
python3 benchmarks/scale_test.py latency -c 100 -t 10 \
  | tee results/scale-latency-<轮次>-$(date +%Y%m%d-%H%M%S).md
python3 benchmarks/scale_test.py latency -c 50 -t 10 --payload chat --rooms 4 \
  | tee results/scale-chat-<轮次>-$(date +%Y%m%d-%H%M%S).md

# 3) 把数字填进下面的表，原始文件路径也写上
```

> `scale_test.py` 只把报告打印到 stdout、**不写文件**，所以用 `tee` 留档（文件名带轮次）。
> 它认 `SERVER_BIN` 环境变量：对照参考实现时设 `SERVER_BIN=build-ref/bin/gomoku_server`。

## 对比表

| 轮次 | TAG | 假设 / 改了什么 | webbench 峰值 QPS (c=10) | max-online | PING p50 @c=100 (ms) | CHAT p99 @c=50 (ms) | 结论 | 保留? | 结果文件 |
|---|---|---|---:|---:|---:|---:|---|---|---|
| 参考 | baseline | forDsh 参考实现（未改动） | 35128 | 20000 | 1.368 | 21.0 | 基准线 | – | `results/baseline-forDsh-webbench-20261018-153703.md` |
| 0 | self-base | 自研 muduo，与参考实现等价 | | | | | | | |
| 1 | | | | | | | | | |
| 2 | | | | | | | | | |

填表要求：

- **一次只改一个假设**。一轮里混两个改动，数字变化就说不清是谁的功劳。
- **"保留?" 只有三种答案**：`是`（合入）、`否`（回滚，写明为什么）、`待定`（需重复测）。
- 结论要写成可证伪的一句，例如"峰值 QPS +8%，p99 不变 → eventfd 合并唤醒有效"。
  不要写"性能有所提升"这种没法验证的话。
- 数字异常（比基线差很多、或某个档位失败）时，先看 `results/raw-*/webbench-c*.txt`。

## 固定口径（换口径就要重测基线）

| 项 | 固定值 | 原因 |
|---|---|---|
| 机器 | 同一台、同一 WSL2 | 跨机器数字不可比 |
| 服务端参数 | `--io-threads 4`（除非本轮就是测线程数） | 线程数直接决定吞吐 |
| 单档时长 | 10 秒 | 太短受启动抖动影响 |
| `ulimit -n` | 65535（服务端与压测端都要） | 决定最大在线数 |
| 日志目录 | `--log-dir /tmp/...` | 9p 磁盘会拖慢 AsyncLogging |
| 端口 | 做 A/B 时**前后跑**，不并行 | 并行会互相抢 CPU |
| 客户端 | 同一个 `benchmarks/` 脚本版本 | 工具变了数字就没意义 |
| 负载 | 压测与对局不要同时进行 | 混跑无法归因 |

## 已知的口径陷阱

1. **认证吞吐才是硬瓶颈**（约 210–250 次/秒）：`max-online` 主要受 MySQL 单连接 + 每事务 fsync 限制，
   不是网络层。想提升它要改 `Db`（应用层），不算 muduo 优化。
2. **首次运行更慢**：账号不存在时走 `REGISTER`，之后走 `LOGIN`。比 `max-online` 时确认账号已存在。
3. **HTTP 端点单 IO 线程 + 每请求短连接**：QPS 会平台化，这是参考设计本身的特性；
   若你想改这块，它属于 `HttpStatusServer`（应用层），要单独列一轮。
4. **webbench 必须 HTTP/1.0（默认，别加 `-2`）且 URL 以 `/` 结尾**，否则失败数会爆。
5. **p99 才有意义**：单次离群点（首轮建连、调度抖动）会把 max 拉得很夸张。
