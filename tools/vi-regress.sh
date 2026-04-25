#!/bin/bash
# Post-build check for vi user program: binary exists and exports main.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="${ROOT}/build"

if [[ ! -f "${B}/_vi" ]]; then
  echo "vi-regress: missing ${B}/_vi" >&2
  exit 1
fi
if command -v i686-linux-gnu-nm >/dev/null 2>&1; then
  i686-linux-gnu-nm "${B}/_vi" | grep -q ' T main'
elif command -v nm >/dev/null 2>&1; then
  nm "${B}/_vi" | grep -q ' T main'
else
  echo "vi-regress: nm not found" >&2
  exit 1
fi
echo "vi-regress: ok"
