#!/usr/bin/env bash
# ============================================================================
# webbench 并发矩阵压测
#
# 做法：对 Gomoku 服务端的 HTTP 状态端点（默认 8080）做多档并发压测，
#       每一档跑 DURATION 秒，记录 QPS / 失败数 / 吞吐，并输出 Markdown + CSV。
#
# 用法：
#   ./benchmarks/run_webbench.sh                      # 默认 1/10/50/100/200/500 并发，10 秒
#   DURATION=5 LEVELS="10 100 1000" ./benchmarks/run_webbench.sh
#   NO_START=1 ./benchmarks/run_webbench.sh           # 复用已在运行的服务端
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${HOST:-127.0.0.1}"
GAME_PORT="${GAME_PORT:-9527}"
HTTP_PORT="${HTTP_PORT:-8080}"
IO_THREADS="${IO_THREADS:-4}"
DURATION="${DURATION:-10}"
LEVELS="${LEVELS:-1 10 50 100 200 500}"
WB="${WB:-$ROOT_DIR/tools/webbench/webbench}"
SERVER_BIN="$ROOT_DIR/build/bin/gomoku_server"
OUT_DIR="$ROOT_DIR/benchmarks/results"
STAMP="$(date +%Y%m%d-%H%M%S)"
MD="$OUT_DIR/webbench-$STAMP.md"
CSV="$OUT_DIR/webbench-$STAMP.csv"
RAW_DIR="$OUT_DIR/raw-$STAMP"
LOG_DIR="${LOG_DIR:-/tmp/gomoku-bench-$STAMP}"
SERVER_PID=""

cleanup() {
  if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

if [ ! -x "$WB" ]; then
  echo "==> webbench 未编译，先执行 make"
  make -C "$ROOT_DIR/tools/webbench"
fi
if [ ! -x "$SERVER_BIN" ]; then
  echo "找不到 $SERVER_BIN，请先执行 ./scripts/build.sh" >&2
  exit 1
fi

mkdir -p "$OUT_DIR" "$RAW_DIR"

# 端口已有人监听就复用；否则临时拉起一个
if [ "${NO_START:-0}" = "1" ]; then
  echo "==> NO_START=1，直接使用已运行的服务端 $HOST:$HTTP_PORT"
elif (exec 3<>"/dev/tcp/$HOST/$GAME_PORT") 2>/dev/null; then
  echo "==> 复用已在运行的服务端 $HOST:$GAME_PORT（HTTP 端口 $HTTP_PORT）"
else
  echo "==> 启动服务端：游戏 $GAME_PORT / HTTP $HTTP_PORT / IO 线程 $IO_THREADS"
  mkdir -p "$LOG_DIR"
  "$SERVER_BIN" --listen 0.0.0.0 --port "$GAME_PORT" --http-port "$HTTP_PORT" \
                 --io-threads "$IO_THREADS" --log-dir "$LOG_DIR" >/dev/null 2>&1 &
  SERVER_PID=$!
  for _ in $(seq 1 100); do
    if (exec 3<>"/dev/tcp/$HOST/$GAME_PORT") 2>/dev/null; then break; fi
    sleep 0.1
  done
fi

# 压测高并发必须提高文件句柄上限
ulimit -n 65535 2>/dev/null || echo "警告：ulimit -n 设置失败，高并发档位可能失败"

# 环境信息（便于结果可复现）
CPU_MODEL="$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
NPROC="$(nproc)"
KERNEL="$(uname -r)"
MEM="$(free -m | awk '/Mem:/{print $2" MB"}')"

{
  echo "# webbench 压测结果"
  echo ""
  echo "- 时间：$STAMP"
  echo "- 目标：http://$HOST:$HTTP_PORT/ （服务端内置 HTTP 状态端点）"
  echo "- 单档时长：${DURATION}s，并发档位：$LEVELS"
  echo "- 服务端：gomoku_server --io-threads $IO_THREADS"
  echo "- 环境：$CPU_MODEL / ${NPROC} 核 / $MEM / 内核 $KERNEL（WSL2 Ubuntu 22.04）"
  echo ""
  echo "| 并发 | QPS (req/s) | 成功请求 | 失败请求 | 传输速率 (MB/s) | pages/min |"
  echo "|---:|---:|---:|---:|---:|---:|"
} > "$MD"
echo "clients,qps,success,failed,mb_per_sec,pages_per_min" > "$CSV"

FAIL_TOTAL=0
for C in $LEVELS; do
  echo "==> 压测并发 $C ..."
  OUT_FILE="$RAW_DIR/webbench-c$C.txt"
  "$WB" -c "$C" -t "$DURATION" "http://$HOST:$HTTP_PORT/" > "$OUT_FILE" 2>&1 || true

  # webbench 的汇总在最后两行：
  #   Speed=2055480 pages/min, 12812418 bytes/sec.
  #   Requests: 171290 susceed, 0 failed.
  SPEED_LINE="$(grep -a 'Speed=' "$OUT_FILE" | tail -1 || true)"
  REQ_LINE="$(grep -a 'Requests:' "$OUT_FILE" | tail -1 || true)"
  PAGES="$(echo "$SPEED_LINE" | sed -n 's/.*Speed=\([0-9]*\) pages\/min.*/\1/p')"
  BYTES="$(echo "$SPEED_LINE" | sed -n 's/.*, *\([0-9]*\) bytes\/sec.*/\1/p')"
  OK_REQ="$(echo "$REQ_LINE" | sed -n 's/.*Requests: *\([0-9]*\) susceed.*/\1/p')"
  FAILED="$(echo "$REQ_LINE" | sed -n 's/.*susceed, *\([0-9]*\) failed.*/\1/p')"

  PAGES="${PAGES:-0}"; BYTES="${BYTES:-0}"; OK_REQ="${OK_REQ:-0}"; FAILED="${FAILED:-0}"
  QPS="$(awk -v p="$PAGES" 'BEGIN{printf "%.0f", p/60}')"
  MBPS="$(awk -v b="$BYTES" 'BEGIN{printf "%.2f", b/1048576}')"
  if [ "$QPS" = "0" ]; then
    echo "    该档没有解析到结果，请查看 $OUT_FILE" >&2
    QPS="N/A"; MBPS="N/A"
  fi
  FAIL_TOTAL=$((FAIL_TOTAL + FAILED))
  echo "    并发 $C -> QPS $QPS，失败 $FAILED"
  echo "|$C|$QPS|$OK_REQ|$FAILED|$MBPS|$PAGES|" >> "$MD"
  echo "$C,$QPS,$OK_REQ,$FAILED,$MBPS,$PAGES" >> "$CSV"
done

{
  echo ""
  echo "失败请求合计：$FAIL_TOTAL"
  echo ""
  echo "## 复现方法"
  echo ""
  printf '\140\140\140bash\n'
  echo "./scripts/build.sh && make -C tools/webbench"
  echo "DURATION=$DURATION LEVELS=\"$LEVELS\" ./benchmarks/run_webbench.sh"
  printf '\140\140\140\n'
} >> "$MD"

echo ""
echo "==> 结果已写入："
echo "    $MD"
echo "    $CSV"
echo "    原始输出目录：$RAW_DIR"
cat "$MD"
