#!/usr/bin/env bash
# ============================================================================
# 启动五子棋服务端
#
# 前台运行：
#   ./scripts/run_server.sh
# 常用参数透传：
#   ./scripts/run_server.sh --port 9527 --http-port 8080 --io-threads 4
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT_DIR/build/bin/gomoku_server}"

if [ ! -x "$BIN" ]; then
  echo "找不到 $BIN，请先执行 scripts/build.sh" >&2
  exit 1
fi

# 提高文件句柄上限，压测高并发时需要（普通运行也无害）
ulimit -n 65535 2>/dev/null || true

exec "$BIN" "$@"
