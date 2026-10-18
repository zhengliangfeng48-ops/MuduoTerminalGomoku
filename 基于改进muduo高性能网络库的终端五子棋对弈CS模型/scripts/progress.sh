#!/usr/bin/env bash
# ============================================================================
# 学习进度检查：把你写的 muduo 和参考实现（forDsh/muduo）比一比
#
# 用法：
#   ./scripts/progress.sh
#   REF_MUDUO=/forDsh/muduo ./scripts/progress.sh      # 指定参考实现位置
#
# 输出：
#   1. 已实现 N/82（.h/.cc 目标数）以及 .cc 数量（能不能被 add_library 收集到）
#   2. 缺失清单，按 base / net / poller 分组
#   3. 多出来的文件（可能是你自己拆的模块，正常）
#   4. 与参考实现"逐字相同"的文件数——教学自检用：
#      学习目标是自己写，如果这个数字很高，说明有文件是直接拷贝过来的
#
# 注意：这里比较的是"文件名集合"，不是内容。文件名必须与参考实现一致，
#       因为 Gomoku 的 #include "muduo/net/TcpServer.h" 是按路径写的。
# ============================================================================
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MY="$( cd "$ROOT_DIR/muduo/muduo" 2>/dev/null && pwd || echo '' )"

# ---------------------------------------------------------------------------
# 找参考实现：REF_MUDUO > /forDsh/muduo > 仓库里的 ../forDsh/muduo
# ---------------------------------------------------------------------------
REF_MUDUO="${REF_MUDUO:-}"
if [ -z "$REF_MUDUO" ]; then
  if [ -d /forDsh/muduo/muduo/net ]; then
    REF_MUDUO=/forDsh/muduo
  elif [ -d "$ROOT_DIR/../forDsh/muduo/muduo/net" ]; then
    REF_MUDUO="$ROOT_DIR/../forDsh/muduo"
  fi
fi
if [ -z "$REF_MUDUO" ] || [ ! -d "$REF_MUDUO/muduo/net" ]; then
  cat >&2 <<'EOF'
找不到参考实现 muduo 目录。请用环境变量指定，例如：
  REF_MUDUO=/forDsh/muduo ./scripts/progress.sh
  REF_MUDUO="<仓库>/forDsh/muduo" ./scripts/progress.sh
EOF
  exit 2
fi
REF="$( cd "$REF_MUDUO/muduo" && pwd )"
MY="${MY:-$ROOT_DIR/muduo/muduo}"

echo "参考实现 : $REF"
echo "我的实现 : $MY"
echo

# ---------------------------------------------------------------------------
# 收集两边的相对路径集合（只算 .h / .cc）
# ---------------------------------------------------------------------------
collect() {  # $1 = 根目录
  ( cd "$1" && find base net -type f \( -name '*.h' -o -name '*.cc' \) 2>/dev/null | LC_ALL=C sort )
}

REF_LIST="$(collect "$REF")"
MY_LIST="$(collect "$MY")"

REF_TOTAL=$(printf '%s\n' "$REF_LIST" | grep -c . || true)
REF_CC=$(printf '%s\n' "$REF_LIST" | grep -c '\.cc$' || true)
MY_TOTAL=$(printf '%s\n' "$MY_LIST" | grep -c . || true)
MY_CC=$(printf '%s\n' "$MY_LIST" | grep -c '\.cc$' || true)

# ---------------------------------------------------------------------------
# 逐个比对
# ---------------------------------------------------------------------------
DONE=0
SAME=0
MISSING=""
for rel in $REF_LIST; do
  if [ -f "$MY/$rel" ]; then
    DONE=$((DONE + 1))
    if cmp -s "$REF/$rel" "$MY/$rel"; then
      SAME=$((SAME + 1))
    fi
  else
    MISSING="$MISSING$rel\n"
  fi
done

# 多出来的文件
EXTRA=""
for rel in $MY_LIST; do
  if [ ! -f "$REF/$rel" ]; then
    EXTRA="$EXTRA$rel\n"
  fi
done

BAR_WIDTH=40
FILLED=$(( DONE * BAR_WIDTH / (REF_TOTAL > 0 ? REF_TOTAL : 1) ))
BAR=""
i=0
while [ $i -lt $BAR_WIDTH ]; do
  if [ $i -lt $FILLED ]; then BAR="$BAR#"; else BAR="$BAR-"; fi
  i=$((i + 1))
done

printf '进度: [%s] %d/%d 个文件\n' "$BAR" "$DONE" "$REF_TOTAL"
printf '      .cc: %d/%d（能编译进去的实现文件数）\n' "$MY_CC" "$REF_CC"
printf '      逐字相同: %d 个（学习自检，偏多说明有文件是直接拷的）\n' "$SAME"
echo

if [ -n "$MISSING" ]; then
  echo "## 还缺的文件"
  for group in base net/poller net; do
    items="$(printf "$MISSING" | grep -E "^${group}/[^/]+$" || true)"
    if [ -n "$items" ]; then
      echo "  [$group]"
      printf '%s\n' "$items" | sed 's/^/    /'
    fi
  done
  echo
fi

if [ -n "$EXTRA" ]; then
  echo "## 参考实现里没有的文件（自己拆的模块，正常）"
  printf "$EXTRA" | sed 's/^/    /'
  echo
fi

if [ "$DONE" -eq "$REF_TOTAL" ]; then
  echo "==> 82 个文件齐了。下一步：cmake --build build（或先跑 ./build/bin/gomoku_board_test）"
else
  echo "==> 还没齐，先按 ./AGENTS.md 里的阶段顺序写；缺的文件就是上面那份清单。"
fi
