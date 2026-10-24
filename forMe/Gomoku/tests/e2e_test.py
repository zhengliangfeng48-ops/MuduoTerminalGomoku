#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gomoku 服务端端到端测试（协议级，只用标准库）

覆盖场景：
  1. 注册 / 重名拒绝 / 未登录拒绝 / 错误密码
  2. 同一昵称重复在线拒绝
  3. 进房（自动建房）、上座、两座坐满自动开局
  4. 轮次校验、非法落子、五连判胜
  5. 对局中观战者聊天被拒、对弈双方聊天可广播
  6. 对局中第三人上座被拒
  7. 起身认输、断线判负
  8. 赛后战绩落库校验（games / wins / win_rate）
  9. 压测 HTTP 端点 /health、/stats

用法：
  python3 Gomoku/tests/e2e_test.py                       # 自动拉起服务端再测试
  python3 Gomoku/tests/e2e_test.py --no-start --port 9527 # 测已经在跑的服务端
  可选：--mysql-cmd "mysql"（默认用 root socket 认证读库做校验）
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

HOST = "127.0.0.1"
PASS = "pw123456"

checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if cond:
        print("  [ok]   %s" % what)
    else:
        failures += 1
        print("  [FAIL] %s" % what)


class Client(object):
    """同步的文本协议客户端：发送一行，按需等待某前缀的消息。"""

    def __init__(self, port, name):
        self.name = name
        self.sock = socket.create_connection((HOST, port), timeout=5)
        self.buf = b""
        self.seen = []

    def send(self, line):
        self.sock.sendall((line + "\n").encode("utf-8"))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def readline(self, timeout):
        """读一行；超时抛 socket.timeout。"""
        self.sock.settimeout(timeout)
        while b"\n" not in self.buf:
            data = self.sock.recv(4096)
            if not data:
                raise EOFError("connection closed by server")
            self.buf += data
        raw, self.buf = self.buf.split(b"\n", 1)
        line = raw.decode("utf-8", "replace").rstrip("\r")
        self.seen.append(line)
        return line

    def wait_for(self, prefix, timeout=6.0):
        """等待以 prefix 开头的消息，返回该行；期间收到的其它消息被忽略。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.readline(max(0.05, deadline - time.time()))
            except socket.timeout:
                continue
            if line.startswith(prefix):
                return line
        raise AssertionError("%s: 等待 %r 超时，已收到: %s" % (self.name, prefix, self.seen[-8:]))

    def expect_err(self, code, timeout=6.0):
        line = self.wait_for("ERR", timeout)
        parts = line.split(" ", 2)
        got = parts[1] if len(parts) > 1 else ""
        check(got == code, "%s 收到错误码 %s（期望 %s）" % (self.name, got, code))
        return line

    def drain(self, seconds=0.3):
        """把当前缓冲区里的消息都读掉（用于“不应该收到消息”的断言）。"""
        out = []
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                out.append(self.readline(max(0.02, deadline - time.time())))
            except (socket.timeout, EOFError):
                break
        return out


def wait_port(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection((HOST, port), timeout=0.5)
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def http_get(port, path):
    with urllib.request.urlopen("http://%s:%d%s" % (HOST, port, path), timeout=5) as resp:
        return resp.status, resp.read().decode("utf-8", "replace")


def mysql_query(mysql_cmd, sql):
    """用 mysql CLI 读库（root socket 认证）。返回行列表（制表符分隔的字段）。"""
    try:
        out = subprocess.check_output([mysql_cmd, "-N", "-B", "-e", sql], stderr=subprocess.STDOUT,
                                      timeout=10)
    except (subprocess.CalledProcessError, OSError) as exc:
        raise RuntimeError("mysql 查询失败: %s (%s)" % (sql, exc))
    text = out.decode("utf-8", "replace").strip()
    return [line.split("\t") for line in text.splitlines() if line]


def stats_of(mysql_cmd, nick):
    rows = mysql_query(mysql_cmd, "SELECT games, wins, win_rate FROM gomoku.users "
                                  "WHERE nickname='%s'" % nick)
    if not rows:
        return None
    return int(rows[0][0]), int(rows[0][1]), rows[0][2]


def wait_stats(mysql_cmd, nick, expected, timeout=6.0):
    """服务端先广播 OVER 再写库，因此这里轮询等待战绩落库。"""
    deadline = time.time() + timeout
    got = None
    while time.time() < deadline:
        got = stats_of(mysql_cmd, nick)
        if got == expected:
            return got
        time.sleep(0.1)
    return got


def main():
    global failures
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=19527)
    parser.add_argument("--http-port", type=int, default=18080)
    parser.add_argument("--no-start", action="store_true", help="不自动启动服务端")
    parser.add_argument("--server", default=os.path.join(os.path.dirname(__file__), "..", "..",
                                                        "build", "bin", "gomoku_server"))
    parser.add_argument("--mysql-cmd", default="mysql")
    parser.add_argument("--skip-db", action="store_true", help="跳过 MySQL 校验")
    args = parser.parse_args()

    suffix = str(int(time.time()))[-6:]
    na = "a" + suffix
    nb = "b" + suffix
    ns = "s" + suffix
    nt = "t" + suffix
    room = "r" + suffix

    proc = None
    logdir = tempfile.mkdtemp(prefix="gomoku-e2e-")
    if not args.no_start:
        server = os.path.abspath(args.server)
        if not os.path.exists(server):
            print("找不到服务端二进制 %s，请先执行 scripts/build.sh" % server)
            return 2
        print("==> 启动服务端 %s" % server)
        proc = subprocess.Popen([server,
                                 "--port", str(args.port),
                                 "--http-port", str(args.http_port),
                                 "--io-threads", "2",
                                 "--log-dir", os.path.join(logdir, "logs")],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if not wait_port(args.port):
            print("服务端启动失败（端口未就绪）")
            out = proc.stdout.read().decode("utf-8", "replace") if proc.stdout else ""
            print(out[-3000:])
            proc.kill()
            return 2
        print("==> 服务端已就绪")
    else:
        print("==> 使用已运行的服务端 127.0.0.1:%d" % args.port)

    skipped_db = args.skip_db
    if not skipped_db:
        try:
            mysql_query(args.mysql_cmd, "SELECT 1")
        except RuntimeError as exc:
            print("警告：无法连接 MySQL，跳过战绩校验（%s）" % exc)
            skipped_db = True

    clients = []
    try:
        print("\n[1] 未登录命令应被拒绝")
        t0 = Client(args.port, "anon")
        clients.append(t0)
        t0.send("JOIN " + room)
        t0.expect_err("NOT_LOGIN")
        t0.send("CHAT hello")
        t0.expect_err("NOT_LOGIN")
        t0.close()
        clients.remove(t0)

        print("\n[2] 注册两位玩家")
        a = Client(args.port, na); clients.append(a)
        b = Client(args.port, nb); clients.append(b)
        a.send("REGISTER %s %s" % (na, PASS))
        line = a.wait_for("AUTH")
        check(line.split()[1] == na, "AUTH 返回昵称 %s" % na)
        check(line.split()[2] == "0" and line.split()[3] == "0", "新用户初始场次/胜场为 0")
        b.send("REGISTER %s %s" % (nb, PASS))
        b.wait_for("AUTH")

        print("\n[3] 昵称唯一性 / 重复在线 / 错误密码")
        c = Client(args.port, "dup"); clients.append(c)
        c.send("REGISTER %s %s" % (na, PASS))
        c.expect_err("NAME_TAKEN")
        c.send("LOGIN %s %s" % (na, PASS))
        c.expect_err("ALREADY_ONLINE")
        c.close(); clients.remove(c)

        # 用一个注册后立刻断开的账号测“错误密码”，避免命中 ALREADY_ONLINE
        t = Client(args.port, nt); clients.append(t)
        t.send("REGISTER %s %s" % (nt, PASS))
        t.wait_for("AUTH")
        t.send("QUIT")
        t.wait_for("OK")
        t.close(); clients.remove(t)
        time.sleep(0.2)
        t = Client(args.port, nt + "-2"); clients.append(t)
        t.send("LOGIN %s %s" % (nt, "wrongpass"))
        t.expect_err("BAD_PASSWORD")
        t.send("LOGIN %s %s" % ("nosuchuser" + suffix, PASS))
        t.expect_err("NO_SUCH_USER")
        t.close(); clients.remove(t)

        print("\n[4] 进房（自动建房）、上座、坐满自动开局")
        a.send("JOIN " + room)
        a.wait_for("ROOM")
        a.send("SIT")
        a.wait_for("SEATS " + na)      # 确认黑位已经坐稳，避免与 b 的操作竞争
        b.send("JOIN " + room)
        b.wait_for("ROOM")
        b.send("SIT")
        # b 坐下后两边都应收到 START
        line = b.wait_for("START")
        check(line.split()[1] == na and line.split()[2] == nb,
              "自动开局，黑=%s 白=%s" % (na, nb))
        line_a = a.wait_for("START")
        check(line_a.split()[1] == na, "黑方也收到 START")
        check(a.wait_for("TURN").endswith(na), "黑方先手")

        print("\n[5] 轮次校验与非法落子")
        b.send("MOVE 10 10")          # 白方抢先落子
        b.expect_err("NOT_YOUR_TURN")
        a.send("MOVE 16 1")           # 越界
        a.expect_err("BAD_ARG")
        a.send("MOVE 1 1")
        a.wait_for("PLACED 1 1")
        b.send("MOVE 1 1")            # 占位
        b.expect_err("ILLEGAL_MOVE")

        print("\n[6] 五连判胜 + 战绩落库")
        # 黑已经在 (1,1) 落子，现在轮到白。
        # 黑的目标：主对角线 (1,1)(2,2)(3,3)(4,4)(5,5)；
        # 白只在第 15 行放 4 子作陪衬（4 子不会成五）。
        b.send("MOVE 1 15")
        b.wait_for("PLACED 1 15")
        for x in (2, 3, 4):
            a.send("MOVE %d %d" % (x, x))
            a.wait_for("PLACED %d %d" % (x, x))
            b.send("MOVE %d 15" % x)
            b.wait_for("PLACED %d 15" % x)
        a.send("MOVE 5 5")
        a.wait_for("PLACED 5 5")
        line = a.wait_for("OVER")
        parts = line.split()
        check(parts[1] == "WON" and parts[2] == na and parts[3] == "FIVE",
              "OVER WON %s FIVE（实际 %s）" % (na, line))
        b.wait_for("OVER")
        a.wait_for("INFO")
        b.wait_for("INFO")
        if not skipped_db:
            st = wait_stats(args.mysql_cmd, na, (1, 1, "1.0000"))
            check(st == (1, 1, "1.0000"), "黑方战绩 games=1 wins=1 rate=1.0000（实际 %s）" % (st,))
            st = wait_stats(args.mysql_cmd, nb, (1, 0, "0.0000"))
            check(st == (1, 0, "0.0000"), "白方战绩 games=1 wins=0 rate=0.0000（实际 %s）" % (st,))
            rows = mysql_query(args.mysql_cmd, "SELECT result, reason, moves FROM gomoku.match_history "
                                               "WHERE room_id='%s' ORDER BY id" % room)
            check(bool(rows) and rows[-1][0] == "black" and rows[-1][1] == "FIVE",
                  "match_history 记录黑胜 FIVE（实际 %s）" % (rows[-1] if rows else None))

        print("\n[7] 赛后再战：READY 流程 + 观战聊天限制")
        a.send("READY")
        b.send("READY")
        a.wait_for("START")
        b.wait_for("START")
        s = Client(args.port, ns); clients.append(s)
        s.send("REGISTER %s %s" % (ns, PASS))
        s.wait_for("AUTH")
        s.send("JOIN " + room)
        s.wait_for("ROOM")
        s.send("SIT")                 # 对局中上座应被拒
        s.expect_err("GAME_RUNNING")
        s.send("CHAT 我是观战者")
        s.expect_err("CHAT_LOCKED")
        s.send("MOVE 1 2")            # 观战者落子
        s.expect_err("NOT_SEATED")
        a.send("CHAT 黑方说话")
        check(a.wait_for("CHAT").endswith("黑方说话"), "对弈双方可以聊天")
        line = s.wait_for("CHAT")
        check("黑方说话" in line, "观战者能收到对局双方的聊天")

        print("\n[8] 起身认输")
        a.send("STAND")
        line = b.wait_for("OVER")
        parts = line.split()
        check(parts[1] == "WON" and parts[2] == nb and parts[3] == "FORFEIT_STAND",
              "黑方起身 → 白方胜（实际 %s）" % line)
        if not skipped_db:
            st = wait_stats(args.mysql_cmd, na, (2, 1, "0.5000"))
            check(st == (2, 1, "0.5000"), "黑方 2 场 1 胜、胜率 0.5000（实际 %s）" % (st,))
            st = wait_stats(args.mysql_cmd, nb, (2, 1, "0.5000"))
            check(st == (2, 1, "0.5000"), "白方 2 场 1 胜、胜率 0.5000（实际 %s）" % (st,))

        print("\n[9] 断线判负")
        b.send("STAND")
        b.wait_for("SEATS - -")       # 两个座位都空出来
        a.send("SIT")
        a.wait_for("SEATS " + na)     # 确认 a 先坐黑位
        s.send("SIT")                 # 观战者上白座
        line = a.wait_for("START")
        parts_start = line.split()
        check(len(parts_start) >= 3 and parts_start[1] == na and parts_start[2] == ns,
              "新对局：黑=%s 白=%s（实际 %s）" % (na, ns, line))
        s.close(); clients.remove(s)
        line = a.wait_for("OVER")
        parts = line.split()
        check(parts[1] == "WON" and parts[2] == na and parts[3] == "FORFEIT_LEFT",
              "对手断线 → 黑方胜（实际 %s）" % line)
        if not skipped_db:
            st = wait_stats(args.mysql_cmd, na, (3, 2, "0.6667"))
            check(st == (3, 2, "0.6667"), "黑方 3 场 2 胜 胜率 0.6667（实际 %s）" % (st,))

        print("\n[10] 房间查询与错误处理")
        a.send("ROOMS")
        line = a.wait_for("ROOMS")
        check(room in line, "ROOMS 列表包含当前房间")
        a.send("WHO")
        check("USERS" in a.wait_for("USERS"), "WHO 返回成员列表")
        a.send("JOIN " + room + "x2")
        a.expect_err("ALREADY_IN_ROOM")
        a.send("JOIN bad-room!")
        a.expect_err("BAD_ARG")
        a.send("LEAVE")
        a.wait_for("OK")
        a.send("BOARD")
        a.expect_err("NOT_IN_ROOM")

        print("\n[11] 压测 HTTP 端点")
        code, body = http_get(args.http_port, "/health")
        check(code == 200 and "ok" in body, "/health 返回 200")
        code, body = http_get(args.http_port, "/stats")
        stats = json.loads(body)
        check(stats.get("status") == "ok", "/stats 返回合法 JSON")
        check(stats.get("rooms", 0) >= 1, "/stats 中 rooms >= 1（实际 %s）" % stats.get("rooms"))
        if not skipped_db:
            check(stats.get("registered_users", -1) >= 4,
                  "/stats 中 registered_users >= 4（实际 %s）" % stats.get("registered_users"))

        print("\n===== %d checks, %d failures =====" % (checks, failures))
        if failures == 0:
            print("PASS")
        else:
            print("FAILED")
        return 0 if failures == 0 else 1
    finally:
        for c in clients:
            c.close()
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
            print("==> 服务端日志保存在 %s" % os.path.join(logdir, "logs"))


if __name__ == "__main__":
    sys.exit(main())
