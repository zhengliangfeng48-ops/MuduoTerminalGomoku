#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最大在线客户端数 与 客户端往返延迟 测试

两个子命令：

  max-online  逐步提升并发连接数，每档都完成认证后再查询服务端 /stats 校验在线数，
              记录"最大可稳定在线的客户端数"。
  latency     固定并发连接数，各连接以"一次只发一个请求、收到应答再发下一个"
              的闭环节奏，
              统计端到端往返时延（p50/p95/p99/avg/max）。

用法：
  python3 benchmarks/scale_test.py max-online --levels 100 500 1000 2000 4000 8000
  python3 benchmarks/scale_test.py latency -c 100 -t 10
  python3 benchmarks/scale_test.py latency -c 100 -t 10 --payload chat --rooms 4

说明：
  * 脚本会自己拉起服务端（并把服务端与自己的 ulimit -n 提到 65535），
    加 --no-start 则复用已在运行的服务端；
  * 账号可复用：首次 REGISTER，之后重跑自动改走 LOGIN；
  * 只依赖 Python 标准库。
"""

import argparse
import json
import os
import resource
import selectors
import socket
import statistics
import subprocess
import sys
import time
import urllib.request

PASSWORD = "scale1234"


# ---------------------------------------------------------------------------
# 基础设施
# ---------------------------------------------------------------------------
def raise_nofile(limit=65535):
    """把本进程与子进程的文件句柄上限抬高，否则几千连接就开不出来。"""
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        want = limit if hard in (resource.RLIM_INFINITY, -1) else min(limit, hard)
        resource.setrlimit(resource.RLIMIT_NOFILE, (want, hard))
    except Exception:
        pass
    return resource.getrlimit(resource.RLIMIT_NOFILE)[0]


def set_child_nofile():
    try:
        resource.setrlimit(resource.RLIMIT_NOFILE, (65535, 65535))
    except Exception:
        pass


class Conn(object):
    """一条已完成 TCP 连接的轻量封装（阻塞 socket + selector 驱动读）。"""

    __slots__ = ("sock", "buf", "nick", "authed", "tried_login",
                 "sent_at", "rtts", "errors", "token", "seq")

    def __init__(self, sock, nick):
        self.sock = sock
        self.buf = b""
        self.nick = nick
        self.authed = False
        self.tried_login = False
        self.sent_at = 0.0
        self.rtts = []
        self.errors = 0
        self.token = ""
        self.seq = 0

    def send(self, line):
        try:
            self.sock.sendall((line + "\n").encode("utf-8"))
            return True
        except OSError:
            self.errors += 1
            return False

    def read_lines(self):
        """把 socket 里当前可读的数据全部读出来，返回完整的行列表。"""
        out = []
        try:
            data = self.sock.recv(16384)
        except OSError:
            return out
        if not data:
            return out
        self.buf += data
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            out.append(raw.decode("utf-8", "replace").rstrip("\r"))
        return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def get_stats(port):
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/stats" % port, timeout=5) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except Exception:
        return {}


def start_server(args):
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    binary = os.path.join(root, "build", "bin", "gomoku_server")
    if not os.path.exists(binary):
        print("找不到 %s，请先执行 ./scripts/build.sh" % binary)
        sys.exit(2)
    if args.no_start:
        return None
    logdir = "/tmp/gomoku-scale-%d" % int(time.time())
    os.makedirs(logdir, exist_ok=True)
    proc = subprocess.Popen(
        [binary, "--port", str(args.port), "--http-port", str(args.http_port),
         "--io-threads", str(args.io_threads), "--log-dir", logdir],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        preexec_fn=set_child_nofile)
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", args.port), timeout=0.5)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    print("服务端启动失败")
    sys.exit(2)


def stop_server(proc):
    if proc is None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


class Pool(object):
    """selector 驱动的连接集合。"""

    def __init__(self):
        self.sel = selectors.DefaultSelector()
        self.conns = []

    def add(self, conn):
        self.sel.register(conn.sock, selectors.EVENT_READ, conn)
        self.conns.append(conn)
        return conn

    def connect(self, host, port, nick, timeout=5.0):
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return self.add(Conn(sock, nick))

    def pump(self, duration, on_line=None):
        """在 duration 秒内持续读事件，逐行回调。"""
        deadline = time.time() + duration
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                return
            for key, _ in self.sel.select(min(0.05, remaining)):
                conn = key.data
                for line in conn.read_lines():
                    if on_line is not None:
                        on_line(conn, line)

    def close_all(self):
        for conn in self.conns:
            try:
                self.sel.unregister(conn.sock)
            except (KeyError, ValueError):
                pass
            conn.close()
        self.conns = []

    def __len__(self):
        return len(self.conns)


def authenticate(pool, conns, timeout):
    """对一批连接完成认证：先 REGISTER，重名则回退 LOGIN。"""
    for c in conns:
        c.send("REGISTER %s %s" % (c.nick, PASSWORD))

    pending = set(id(c) for c in conns)
    by_id = dict((id(c), c) for c in conns)
    deadline = time.time() + timeout

    def on_line(conn, line):
        if conn.authed:
            return
        if line.startswith("AUTH"):
            conn.authed = True
            pending.discard(id(conn))
        elif line.startswith("ERR"):
            if "NAME_TAKEN" in line and not conn.tried_login:
                conn.tried_login = True
                conn.send("LOGIN %s %s" % (conn.nick, PASSWORD))
            else:
                conn.errors += 1
                pending.discard(id(conn))

    while pending and time.time() < deadline:
        pool.pump(0.05, on_line)
    return sum(1 for c in conns if c.authed)


# ---------------------------------------------------------------------------
# 子命令：最大在线数
# ---------------------------------------------------------------------------
def mode_max_online(args):
    """逐档**增量**加压：已有连接保持不动，只补新增的那部分，
    这样认证次数总量只等于最大档位，而不是各档之和。"""
    hard = raise_nofile()
    print("==> 本地 ulimit -n = %d" % hard)
    proc = start_server(args)
    pool = Pool()
    results = []
    peak = 0
    current = 0
    try:
        for target in args.levels:
            if target <= current:
                continue
            delta = target - current

            # 1) 建连（只建增量部分）
            t0 = time.time()
            new_conns = []
            connect_failed = 0
            for i in range(current, target):
                try:
                    new_conns.append(pool.connect(args.host, args.port, "%s%d" % (args.prefix, i)))
                except OSError:
                    connect_failed += 1
            t_conn = time.time() - t0

            # 2) 认证（首次 REGISTER，之后重跑自动 LOGIN）
            t1 = time.time()
            authed = authenticate(pool, new_conns, args.timeout)
            t_auth = time.time() - t1

            # 3) 稳定观察后向 /stats 要真实在线数
            pool.pump(args.hold)
            online = get_stats(args.http_port).get("online", -1)
            auth_failed = sum(c.errors for c in new_conns)

            row = {
                "level": target, "delta": delta, "connected": len(pool),
                "connect_failed": connect_failed, "authed": authed,
                "auth_failed": auth_failed, "connect_sec": round(t_conn, 2),
                "auth_sec": round(t_auth, 2), "stats_online": online,
            }
            results.append(row)
            print("    目标 %6d | 新增 %5d | 已连 %6d | 本档认证 %5d | /stats online %6d | "
                  "建连 %.2fs 认证 %.2fs"
                  % (target, delta, len(pool), authed, online, t_conn, t_auth))

            if authed == delta and online == target:
                peak = target
                current = target
            else:
                print("    该档未能全部稳定在线（建连失败 %d，认证失败 %d），停止继续加压"
                      % (connect_failed, auth_failed))
                break
    finally:
        pool.close_all()
        stop_server(proc)

    print("")
    print("========== 最大在线客户端数 ==========")
    print("| 目标在线 | 新增连接 | 实际连上 | 新增认证成功 | /stats 在线 | 建连耗时(s) | 认证耗时(s) |")
    print("|---:|---:|---:|---:|---:|---:|---:|")
    for r in results:
        print("| %d | %d | %d | %d | %d | %.2f | %.2f |"
              % (r["level"], r["delta"], r["connected"], r["authed"],
                 r["stats_online"], r["connect_sec"], r["auth_sec"]))
    print("")
    print("最大稳定在线客户端数：%d" % peak)
    if results and peak == results[-1]["level"]:
        print("（已测到脚本设定的最高档位，实际上限可能更高）")
    return 0 if peak > 0 else 1


# ---------------------------------------------------------------------------
# 子命令：客户端往返延迟
# ---------------------------------------------------------------------------
def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    k = int(round((pct / 100.0) * (len(sorted_values) - 1)))
    return sorted_values[k]


def mode_latency(args):
    raise_nofile()
    proc = start_server(args)
    pool = Pool()
    rtts = []
    try:
        for i in range(args.clients):
            pool.connect(args.host, args.port, "%s%d" % (args.prefix, i))
        authed = authenticate(pool, pool.conns, args.timeout)
        if authed < args.clients:
            print("警告：只有 %d/%d 个连接认证成功" % (authed, args.clients))
        alive = [c for c in pool.conns if c.authed]

        payload = args.payload
        if payload == "chat":
            for idx, c in enumerate(alive):
                c.send("JOIN lat%02d" % (idx % args.rooms))
            pool.pump(0.5)

        deadline = time.time() + args.duration

        def send_one(conn):
            conn.sent_at = time.time()
            if payload == "chat":
                conn.seq += 1
                conn.token = "tok-%d-%d" % (id(conn) % 100000, conn.seq)
                conn.send("CHAT " + conn.token)
            else:
                conn.send("PING")

        # 闭环节奏：每个连接同时只有一个未完成的请求
        for c in alive:
            send_one(c)

        def on_line(conn, line):
            if not conn.authed or conn.sent_at <= 0:
                return
            if payload == "chat":
                if not (line.startswith("CHAT ") and conn.token and conn.token in line):
                    return
            elif line != "PONG":
                return
            now = time.time()
            conn.rtts.append((now - conn.sent_at) * 1000.0)
            conn.sent_at = 0.0
            if now < deadline:
                send_one(conn)

        while time.time() < deadline:
            pool.pump(0.05, on_line)

        for c in alive:
            rtts.extend(c.rtts)
    finally:
        pool.close_all()
        stop_server(proc)

    rtts.sort()
    total = len(rtts)
    elapsed = args.duration
    print("")
    print("========== 客户端往返延迟（payload=%s）==========" % args.payload)
    print("并发连接        : %d" % args.clients)
    print("压测时长        : %.1f s" % elapsed)
    print("往返样本数      : %d" % total)
    print("吞吐 (req/s)    : %.1f" % (total / elapsed if elapsed else 0))
    if rtts:
        print("平均 / 最小 / 最大 : %.3f / %.3f / %.3f ms"
              % (statistics.mean(rtts), rtts[0], rtts[-1]))
        print("p50 / p95 / p99 : %.3f / %.3f / %.3f ms"
              % (percentile(rtts, 50), percentile(rtts, 95), percentile(rtts, 99)))
    print("================================================")
    print("%d,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f"
          % (args.clients, total / elapsed if elapsed else 0,
             statistics.mean(rtts) if rtts else 0,
             percentile(rtts, 50), percentile(rtts, 95), percentile(rtts, 99),
             rtts[-1] if rtts else 0))
    return 0


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="最大在线数 / 客户端延迟测试")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9527)
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--io-threads", type=int, default=4)
    ap.add_argument("--no-start", action="store_true", help="复用已在运行的服务端")
    ap.add_argument("--prefix", default="scale_")

    sub = ap.add_subparsers(dest="mode", required=True)

    p1 = sub.add_parser("max-online", help="逐步提升并发直到无法全部稳定在线")
    p1.add_argument("--levels", type=int, nargs="+",
                    default=[100, 500, 1000, 2000, 4000, 8000])
    p1.add_argument("--hold", type=float, default=1.0, help="每档稳定观察时间（秒）")
    p1.add_argument("--timeout", type=float, default=120.0, help="每档认证超时（秒）")

    p2 = sub.add_parser("latency", help="固定并发下的往返时延")
    p2.add_argument("-c", "--clients", type=int, default=100)
    p2.add_argument("-t", "--duration", type=float, default=10.0)
    p2.add_argument("--payload", choices=["ping", "chat"], default="ping")
    p2.add_argument("--rooms", type=int, default=4, help="chat 模式下的房间数")
    p2.add_argument("--timeout", type=float, default=120.0)

    args = ap.parse_args()
    if args.mode == "max-online":
        return mode_max_online(args)
    return mode_latency(args)


if __name__ == "__main__":
    sys.exit(main())