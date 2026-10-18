#!/usr/bin/env bash
# ============================================================================
# 初始化 MySQL：启动服务 -> 建库建表建账号 -> 自检
#
# 用法：
#   ./scripts/init_db.sh
#
# 说明：
#   * Ubuntu 的 mysql-server 默认 root 使用 auth_socket 认证，
#     所以以 root 身份执行 'mysql' 即可，无需密码；
#   * 如果 MySQL 已经在别处（例如 Windows 主机）运行，请不要执行本脚本，
#     改为手工执行 sql/schema.sql 并通过 --db-* 参数指向那个实例。
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCHEMA="$ROOT_DIR/Gomoku/sql/schema.sql"

echo "==> [1/4] 启动 MySQL"
if systemctl is-active --quiet mysql 2>/dev/null; then
  echo "    MySQL 已在运行"
else
  if ! (systemctl start mysql 2>/dev/null || service mysql start 2>/dev/null); then
    echo "    无法启动 MySQL，请检查安装：apt install mysql-server" >&2
    exit 1
  fi
  sleep 2
  echo "    MySQL 已启动"
fi

echo "==> [2/4] 建库 / 建表 / 建账号"
mysql < "$SCHEMA"

echo "==> [3/4] 校验表结构"
mysql -e "USE gomoku; SHOW TABLES; DESCRIBE users;"

echo "==> [4/4] 校验服务账号"
mysql -u gomoku -pgomoku123 -e "SELECT COUNT(*) AS user_count FROM gomoku.users;"

echo "==> 数据库初始化完成（默认账号 gomoku / gomoku123，库名 gomoku）"
