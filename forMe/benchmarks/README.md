# 压测（自研工区）

脚本从 `forDsh/benchmarks/` 复制而来，用法与参数完全相同——**方法与解读见
[`../../forDsh/benchmarks/README.md`](../../forDsh/benchmarks/README.md)，这里只记差异**。

## 与参考实现的差异

| 改动 | 用法 |
|---|---|
| `run_webbench.sh` 支持 `TAG` | `TAG=opt1 ./benchmarks/run_webbench.sh` → `results/webbench-opt1-<时间>.md`，头部记录 tag 与二进制指纹 |
| 两个脚本都支持 `SERVER_BIN` | `SERVER_BIN=build-ref/bin/gomoku_server ...` 可压参考实现的二进制，用于 A/B |
| 多了 `RESULTS.md` | 优化前后对比台账，每轮填一行 |

`scale_test.py` 只把报告打到 stdout，留档要重定向；**全局参数必须写在子命令前面**：

```bash
python3 benchmarks/scale_test.py latency -c 100 -t 10 \
  | tee benchmarks/results/scale-latency-<tag>-$(date +%Y%m%d-%H%M%S).md

python3 benchmarks/scale_test.py --port 9627 --http-port 8180 max-online --levels 50   # 对
python3 benchmarks/scale_test.py max-online --levels 50 --port 9627                    # 错
```

## 三项指标的复跑命令

```bash
TAG=<轮次> ./benchmarks/run_webbench.sh                                          # QPS
python3 benchmarks/scale_test.py max-online --levels 1000 2000 4000 8000         # 最大在线
python3 benchmarks/scale_test.py latency -c 100 -t 10 | tee results/scale-latency-<轮次>-<时间>.md
python3 benchmarks/scale_test.py latency -c 50 -t 10 --payload chat --rooms 4 \
  | tee results/scale-chat-<轮次>-<时间>.md                                      # 聊天广播延迟
```

同口径要求（io-threads 4、每档 10s、`ulimit -n 65535`、A/B 同端口先后跑）见
[AGENTS.md](../../AGENTS.md) 的「性能口径」一节；参考基线是 `results/baseline-forDsh-*`，不要改。
