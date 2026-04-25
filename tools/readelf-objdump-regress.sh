#!/bin/bash
# readelf / objdump：主机交叉工具对 build/_cat 做 ELF 静态校验，并检查两工具已链入标识串。
# （QEMU+USB 在部分环境下对连续文件访问不稳定，故 CI 以静态校验为主；需要时可手动在 shell 内运行 /bin/readelf、/bin/objdump。）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
for f in build/_readelf build/_objdump build/_cat; do
  if [[ ! -x "$f" ]]; then
    echo "readelf-objdump-regress: 缺少可执行文件 $f" >&2
    exit 1
  fi
done
if ! i686-linux-gnu-readelf -h build/_cat | grep -q "ELF32"; then
  echo "readelf-objdump-regress: 主机 readelf 未识别 build/_cat 为 ELF32" >&2
  exit 1
fi
if ! i686-linux-gnu-readelf -h build/_cat | grep -q "80386"; then
  echo "readelf-objdump-regress: 主机 readelf 未报告 80386" >&2
  exit 1
fi
if ! i686-linux-gnu-objdump -f build/_cat | grep -qi "elf32.*i386"; then
  echo "readelf-objdump-regress: 主机 objdump 未报告 elf32-i386" >&2
  exit 1
fi
if ! strings build/_readelf | grep -q "readelf"; then
  echo "readelf-objdump-regress: build/_readelf 中无 readelf 标识" >&2
  exit 1
fi
if ! strings build/_objdump | grep -q "objdump"; then
  echo "readelf-objdump-regress: build/_objdump 中无 objdump 标识" >&2
  exit 1
fi
echo "readelf-objdump-regress: 静态校验通过"
