#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
游戏协议并发压测（webbench 只能压 HTTP，压不到自定义对局协议）

每个 worker 线程独立建立一条 TCP 连接，流程：
    连接 -> REGISTER -> JOIN 房间 -> 反复 CHAT（等待广播回显） -> QUIT

统计：完成的聊天往返数、QPS、往返时延 p50/p95/p99、错误数。
由于所有 worker 都是"观战者"（不 SIT），房间不会进入对局，聊天始终被允许。

用法：
  python3 benchmarks/gomoku_bench.py                          # 50 连接 / 10 秒
  python3 benchmarks/gomoku_bench.py -c 200 -t 20 --rooms 4
  python3 benchmarks/gomoku_bench.py --host 127.0.0.1 --port 9527

注意：
  * 会往 MySQL 里创建一批压测账号（前缀 bench_），可用 --prefix 区分；
  * 建议先把服务端 ulimit -n 提到 65535；
  * worker 线程数受 Python GIL 限制，本脚本用于"并发正确性 + 量级评估"，
    极限 QPS 请以 webbench 的 HTTP 数据为准。
"""

import argparse
import random
import socket
import statistics
import sys
import threading
import time

PASSWORD = "bench1234"


class Worker(threading.Thread):
    def __init__(self, args, index, stats, stop_at):
        threading.Thread.__init__(self)
        self.daemon = True
        self.args = args
        self.index = index
        self.stats = stats
        self.stop_at = stop_at
        self.buf = b""
        self.errors = 0
        self.latencies = []
        self.sock = None
        self.nick = "%s%05d" % (args.prefix, index)

    def send(self, line):
        self.sock.sendall((line + "\n").encode("utf-8"))

    def readline(self, timeout):
        self.sock.settimeout(timeout)
        while b"\n" not in self.buf:
            data = self.sock.recv(8192)
            if not data:
                raise EOFError("closed")
            self.buf += data
        raw, self.buf = self.buf.split(b"\n", 1)
        return raw.decode("utf-8", "replace").rstrip("\r")

    def wait_for(self, prefix, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.readline(max(0.02, deadline - time.time()))
            except socket.timeout:
                continue
            if line.startswith(prefix):
                return line
        raise TimeoutError("wait %r timeout" % prefix)

    def wait_any(self, prefixes, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.readline(max(0.02, deadline - time.time()))
            except socket.timeout:
                continue
            for p in prefixes:
                if line.startswith(p):
                    return line
        raise TimeoutError("wait any %r timeout" % (prefixes,))

    def run(self):
        try:
            self.sock = socket.create_connection((self.args.host, self.args.port), timeout=5)
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            # 压测账号是复用的：首次 REGISTER，重复运行时改走 LOGIN，
            # 这样同一批压测账号可以反复压，不会因为 NAME_TAKEN 直接失败。
            self.send("REGISTER %s %s" % (self.nick, PASSWORD))
            line = self.wait_any(["AUTH", "ERR"])
            if line.startswith("ERR"):
                if "NAME_TAKEN" in line:
                    self.send("LOGIN %s %s" % (self.nick, PASSWORD))
                    self.wait_for("AUTH")
                else:
                    raise RuntimeError(line)
            room = "bench%02d" % (self.index % self.args.rooms)
            self.send("JOIN " + room)
            self.wait_for("ROOM")
            with self.stats["lock"]:
                self.stats["joined"] += 1
        except Exception:
            with self.stats["lock"]:
                self.stats["errors"] += 1
            return

        seq = 0
        try:
            while time.time() < self.stop_at:
                seq += 1
                token = "%d-%d-%d" % (self.index, seq, random.randint(0, 999999))
                start = time.time()
                self.send("CHAT " + token)
                # 房间里其他 worker 也在发消息，必须等到自己的 token 回显
                deadline = time.time() + 5.0
                echoed = False
                while time.time() < deadline:
                    try:
                        line = self.readline(max(0.02, deadline - time.time()))
                    except socket.timeout:
                        break
                    if line.startswith("CHAT ") and token in line:
                        echoed = True
                        break
                if echoed:
                    self.latencies.append((time.time() - start) * 1000.0)
                else:
                    self.errors += 1
                time.sleep(self.args.think_time)
        except Exception:
            self.errors += 1
        finally:
            try:
                self.send("QUIT")
                self.sock.close()
            except Exception:
                pass
            with self.stats["lock"]:
                self.stats["messages"] += len(self.latencies)
                self.stats["errors"] += self.errors
                self.stats["latencies"].extend(self.latencies)


def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    k = int(round((pct / 100.0) * (len(sorted_values) - 1)))
    return sorted_values[k]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9527)
    ap.add_argument("-c", "--clients", type=int, default=50)
    ap.add_argument("-t", "--time", dest="duration", type=float, default=10.0)
    ap.add_argument("--rooms", type=int, default=4,
                    help="房间数，workers 会均匀分布到这些房间")
    ap.add_argument("--prefix", default="bench_")
    ap.add_argument("--think-time", type=float, default=0.0,
                    help="每条消息之间的间隔（秒）")
    args = ap.parse_args()

    stats = {"messages": 0, "errors": 0, "joined": 0, "latencies": [], "lock": threading.Lock()}
    start = time.time()
    stop_at = start + args.duration
    workers = [Worker(args, i, stats, stop_at) for i in range(args.clients)]

    print("==> 启动 %d 个连接，对 %s:%d 压测 %.0f 秒（房间数 %d）" %
          (args.clients, args.host, args.port, args.duration, args.rooms))
    for w in workers:
        w.start()
    for w in workers:
        w.join(timeout=args.duration + 30)

    elapsed = time.time() - start
    lat = sorted(stats["latencies"])
    total = stats["messages"]
    qps = total / elapsed if elapsed > 0 else 0.0

    print("")
    print("========== 游戏协议压测结果 ==========")
    print("并发连接      : %d" % args.clients)
    print("实际时长      : %.2f s" % elapsed)
    print("成功入房连接  : %d" % stats["joined"])
    print("聊天往返总数  : %d" % total)
    print("吞吐 (msg/s)  : %.1f" % qps)
    print("错误数        : %d" % stats["errors"])
    if lat:
        print("延迟 p50/p95/p99 : %.2f / %.2f / %.2f ms" %
              (percentile(lat, 50), percentile(lat, 95), percentile(lat, 99)))
        print("延迟 avg/max     : %.2f / %.2f ms" % (statistics.mean(lat), lat[-1]))
    else:
        print("没有成功的聊天往返")
    print("=====================================")
    return 0 if stats["errors"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
