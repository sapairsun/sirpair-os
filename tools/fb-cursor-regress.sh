#!/bin/bash
# 回归：确认帧缓冲光标闪烁实现已链接进内核（console_cursor_tick + fb_paint_cursor）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${ROOT}/build/kernel.elf"
if [[ ! -f "${ELF}" ]]; then
  echo "fb-cursor-regress: 缺少 ${ELF}，请先编译" >&2
  exit 1
fi
TP="${TOOLPREFIX-}"
if [[ -n "${TP}" ]] && command -v "${TP}objdump" >/dev/null 2>&1; then
  OBJDUMP="${TP}objdump"
elif command -v i686-linux-gnu-objdump >/dev/null 2>&1; then
  OBJDUMP=i686-linux-gnu-objdump
else
  OBJDUMP=objdump
fi
# 注意：勿用「objdump | grep -q」在 pipefail 下检测——grep 早退会令 objdump 收 SIGPIPE(141)，管道整体失败。
SYM="$("${OBJDUMP}" -t "${ELF}" 2>/dev/null || true)"
if [[ "${SYM}" != *console_cursor_tick* ]]; then
  echo "fb-cursor-regress: 未找到符号 console_cursor_tick" >&2
  exit 1
fi
if [[ "${SYM}" != *fb_paint_cursor* ]]; then
  echo "fb-cursor-regress: 未找到符号 fb_paint_cursor" >&2
  exit 1
fi
echo "fb-cursor-regress: ok"
exit 0
