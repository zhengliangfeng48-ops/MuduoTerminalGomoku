#!/usr/bin/env bash
# ============================================================================
# 一次性环境准备（Ubuntu / WSL2）
#   * 安装编译与运行依赖
#   * 建立 /forDsh 软链（让需求中的绝对路径 /forDsh/muduo、/forDsh/Gomoku 直接可用）
#   * 打印版本自检信息
# ============================================================================
set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

echo "==> [1/3] 安装依赖"
$SUDO apt-get update
$SUDO apt-get install -y \
  build-essential cmake pkg-config \
  libboost-dev libmysqlclient-dev \
  mysql-server python3

echo "==> [2/3] 建立 /forDsh 软链"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$ROOT_DIR" != "/forDsh" ]; then
  if [ -e /forDsh ] && [ ! -L /forDsh ]; then
    echo "    警告：/forDsh 已存在且不是软链，跳过创建" >&2
  else
    $SUDO ln -sfn "$ROOT_DIR" /forDsh
    echo "    /forDsh -> $(readlink -f /forDsh)"
  fi
else
  echo "    当前目录已经是 /forDsh"
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
