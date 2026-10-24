#!/usr/bin/env bash
# ============================================================================
# 编译整个工程（muduo 核心库 + Gomoku 服务端/客户端/单测）
#
# 用法：
#   ./scripts/build.sh              # Release 构建到 forDsh/build
#   BUILD_TYPE=Debug ./scripts/build.sh
#   BUILD_DIR=/tmp/gb ./scripts/build.sh
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

echo "==> 工程目录 : $ROOT_DIR"
echo "==> 构建目录 : $BUILD_DIR"
echo "==> 构建类型 : $BUILD_TYPE"
echo "==> 并行度   : $JOBS"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "==> 构建产物："
ls -lh "$BUILD_DIR/bin" || true
