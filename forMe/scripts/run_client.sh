#!/usr/bin/env bash
# ============================================================================
# 启动五子棋终端客户端
#
# 用法：
#   ./scripts/run_client.sh                      # 交互式：提示选择注册/登录
#   ./scripts/run_client.sh --host 127.0.0.1 --port 9527
#   ./scripts/run_client.sh --nick alice --password alice123 --register
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT_DIR/build/bin/gomoku_client}"

if [ ! -x "$BIN" ]; then
  echo "找不到 $BIN，请先执行 scripts/build.sh" >&2
  exit 1
fi

exec "$BIN" "$@"
