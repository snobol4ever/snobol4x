#!/bin/bash
# icon_x86_runner.sh — compile .icn → x86 ASM → run, print output to stdout
# Usage: icon_x86_runner.sh <file.icn>
# Used by run_rung01.sh / run_rung03.sh (old-format rung scripts).
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip-cc}"
RT="$ROOT/src/runtime"
ICN_INC="$ROOT/src/frontend/icon"
icn="$1"
base=$(mktemp /tmp/icon_x86_XXXXXX)
"$SCRIP_CC" -icn "$icn" -o "${base}.s" 2>/dev/null
nasm -f elf64 "${base}.s" -o "${base}.o" 2>/dev/null
gcc -nostdlib -no-pie -Wl,--no-warn-execstack \
    "${base}.o" "$ICN_INC/icon_runtime.c" \
    -I"$ICN_INC" -I"$RT/x86" -I"$RT" \
    -o "${base}_bin" -lm 2>/dev/null
"${base}_bin"
rm -f "${base}" "${base}.s" "${base}.o" "${base}_bin"
