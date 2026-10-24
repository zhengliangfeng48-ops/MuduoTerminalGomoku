#!/usr/bin/env bash
# ============================================================================
# 一次性环境准备（Ubuntu / WSL2）——  自研工程版本
#
# 与 forDsh/scripts/setup_env.sh 的差异（只有软链这一处）：
#   forDsh 版会执行 `ln -sfn <自己> /forDsh`，把 /forDsh 指向它的目录。
#   本工程绝对不能这么做——/forDsh 是"参考实现"的入口，
#   被抢走之后 forDsh 的文档、-DUSE_REFERENCE_MUDUO 都会失效。
#
#   所以这里：
#     * 默认建立 /gomoku-self 这个**独立**软链；
#     * 若 /forDsh 已存在（链接或实体），一律不碰；
#     * 软链名可用 SYMLINK=/xxx 覆盖。
#
# 用法：
#   ./scripts/setup_env.sh
#   SYMLINK=/my-gomoku ./scripts/setup_env.sh
# ============================================================================
set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

SYMLINK="${SYMLINK:-/gomoku-self}"

echo "==> [1/3] 安装依赖"
$SUDO apt-get update
$SUDO apt-get install -y \
  build-essential cmake pkg-config \
  libboost-dev libmysqlclient-dev \
  mysql-server python3

echo "==> [2/3] 建立 $SYMLINK 软链（不触碰 /forDsh）"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$ROOT_DIR" != "$SYMLINK" ]; then
  if [ -e "$SYMLINK" ] && [ ! -L "$SYMLINK" ]; then
    echo "    警告：$SYMLINK 已存在且不是软链，跳过创建" >&2
  else
    $SUDO ln -sfn "$ROOT_DIR" "$SYMLINK"
    echo "    $SYMLINK -> $(readlink -f "$SYMLINK")"
  fi
else
  echo "    当前目录已经是 $SYMLINK"
fi
if [ -L /forDsh ]; then
  echo "    /forDsh -> $(readlink -f /forDsh)（参考实现，保持不动）"
else
  echo "    注意：/forDsh 不存在。需要参考实现时可手工建：sudo ln -sfn <forDsh目录> /forDsh"
fi

echo "==> [3/3] 版本自检"
g++ --version | head -n 1
cmake --version | head -n 1
mysql --version
python3 --version
echo "    Boost: $(ls /usr/include/boost/version.hpp >/dev/null 2>&1 && echo ok || echo missing)"
echo "    MySQL client dev: $(mysql_config --version)"
if systemctl is-active --quiet mysql 2>/dev/null; then
  echo "    MySQL 服务: running"
else
  echo "    MySQL 服务: 未运行（执行 scripts/init_db.sh 会自动启动）"
fi
echo "==> 环境准备完成"
