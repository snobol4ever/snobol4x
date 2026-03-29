#!/usr/bin/env bash
# snobol4-jvm — compile + run a .sno file via scrip-cc JVM backend
# Usage: snobol4-jvm <file.sno>
# MONITOR_FIFO env var: if set, trace events written there via JVM runtime
set -euo pipefail

SNO="${1:?Usage: snobol4-jvm <file.sno>}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP_CC="${SCRIP_CC_JVM:-/home/claude/scrip-cc_jvm}"
JASMIN="${JASMIN:-$DIR/src/backend/jvm/jasmin.jar}"
JVM_CACHE="${JVM_CACHE:-/tmp/scrip_cc_jvm_cache}"

mkdir -p "$JVM_CACHE"

base="$(basename "$SNO" .sno)"
dir_hash="$(echo "$SNO" | md5sum | cut -c1-8)"
key="${base}_${dir_hash}"
jfile="$JVM_CACHE/${key}.j"
stamp="$JVM_CACHE/${key}.stamp"

"$SCRIP_CC" -jvm "$SNO" > "$jfile" 2>/dev/null

# Extract classname from first .j line: ".class public <name>"
classname=$(grep '\.class' "$jfile" | head -1 | awk '{print $NF}')

j_md5="$(md5sum "$jfile" | cut -d' ' -f1)"
cached_md5="$(cat "$stamp" 2>/dev/null || echo '')"

if [[ "$j_md5" != "$cached_md5" ]] || [[ ! -f "$JVM_CACHE/${classname}.class" ]]; then
    java -jar "$JASMIN" "$jfile" -d "$JVM_CACHE" >/dev/null 2>&1
    echo "$j_md5" > "$stamp"
fi

exec java -cp "$JVM_CACHE" "$classname"
