#!/bin/bash
# icon_jvm_runner.sh — compile .icn → JVM bytecode via scrip -jvm, run with java
# Usage: icon_jvm_runner.sh <file.icn>
# Used by old-format rung scripts (run_rung01.sh, run_rung03.sh).
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip}"
JASMIN="${JASMIN:-$ROOT/src/backend/jasmin.jar}"
icn="$1"
base=$(mktemp /tmp/icon_jvm_XXXXXX)
"$SCRIP_CC" -icn -jvm "$icn" -o "${base}.j" 2>/dev/null
cls=$(grep -m1 '\.class' "${base}.j" 2>/dev/null | awk '{print $NF}')
timeout 30 java -jar "$JASMIN" "${base}.j" -d /tmp/ >/dev/null 2>&1
timeout 5 java -cp /tmp/ "$cls" 2>/dev/null
rm -f "${base}" "${base}.j" "/tmp/${cls}.class" 2>/dev/null || true
