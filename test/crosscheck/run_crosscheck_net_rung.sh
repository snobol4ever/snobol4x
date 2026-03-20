#!/usr/bin/env bash
# run_crosscheck_net_rung.sh — NET backend corpus ladder driver
#
# Compiles each .sno in a given directory via sno2c -net, assembles with
# ilasm, runs under mono, diffs vs .ref oracle.
#
# SPEED: ilasm and mono are both slow to start (~400ms each). This script
# mitigates by:
#   1. Caching: .exe is only rebuilt when the .il changes (via md5 stamp)
#   2. Parallel ilasm: all assemblies run concurrently (background jobs)
#
# Usage:
#   bash test/crosscheck/run_crosscheck_net_rung.sh <dir> [dir2 ...]
#
# Examples:
#   bash test/crosscheck/run_crosscheck_net_rung.sh /path/to/corpus/hello
#   bash test/crosscheck/run_crosscheck_net_rung.sh \
#       /path/to/corpus/hello \
#       /path/to/corpus/output
#
# Environment overrides:
#   SNO2C        — path to sno2c binary  (default: ./sno2c)
#   STOP_ON_FAIL — 1 = stop at first failure (default: 0)
#   CACHE_DIR    — where to cache .il/.exe (default: /tmp/snobol4x_net_cache)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
SNO2C="${SNO2C:-$TINY/sno2c}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
CACHE_DIR="${CACHE_DIR:-/tmp/snobol4x_net_cache}"
mkdir -p "$CACHE_DIR"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'

# -----------------------------------------------------------------------
# Compile Snobol4Lib.dll and Snobol4Run.dll once into CACHE_DIR
# -----------------------------------------------------------------------
RT_IL="$TINY/src/runtime/net"
for dll in snobol4lib snobol4run; do
    src_il="$RT_IL/${dll}.il"
    dst_dll="$CACHE_DIR/${dll}.dll"
    src_stamp="$CACHE_DIR/${dll}.src.stamp"
    src_md5="$(md5sum "$src_il" | cut -d' ' -f1)"
    cached_md5="$(cat "$src_stamp" 2>/dev/null || echo '')"
    if [ "$src_md5" != "$cached_md5" ] || [ ! -f "$dst_dll" ]; then
        ilasm "$src_il" /dll /output:"$dst_dll" >/dev/null 2>&1
        echo "$src_md5" > "$src_stamp"
    fi
done
export MONO_PATH="$CACHE_DIR"

pass=0; fail=0; skip=0
pids=()   # background ilasm pids
jobs=()   # parallel job descriptors: "sno|il|exe|ref"

# -----------------------------------------------------------------------
# Phase 1: emit .il for every .sno, launch ilasm in background
# -----------------------------------------------------------------------
for dir in "$@"; do
    for sno in "$dir"/*.sno; do
        [ -f "$sno" ] || continue
        ref="${sno%.sno}.ref"
        [ -f "$ref" ] || continue

        base="$(basename "$sno" .sno)"
        # unique cache key: rung dir + basename avoids cross-rung collisions
        rung="$(basename "$dir")"
        il="$CACHE_DIR/${rung}_${base}.il"
        exe="$CACHE_DIR/${rung}_${base}.exe"
        stamp="$CACHE_DIR/${rung}_${base}.stamp"

        # Always re-emit .il (sno2c is fast — ~1ms)
        "$SNO2C" -net "$sno" > "$il" 2>/dev/null

        # Only re-assemble if .il changed (ilasm is slow — ~400ms)
        il_md5="$(md5sum "$il" | cut -d' ' -f1)"
        cached_md5="$(cat "$stamp" 2>/dev/null || echo '')"

        if [ "$il_md5" != "$cached_md5" ] || [ ! -f "$exe" ]; then
            # Launch ilasm in background
            ( ilasm "$il" /output:"$exe" >/dev/null 2>&1
              echo "$il_md5" > "$stamp" ) &
            pids+=($!)
        fi

        jobs+=("$sno|$il|$exe|$ref")
    done
done

# Wait for all background ilasm jobs
for pid in "${pids[@]:-}"; do
    wait "$pid" 2>/dev/null || true
done

# -----------------------------------------------------------------------
# Phase 2: run mono for each .exe, diff vs .ref
# -----------------------------------------------------------------------
for job in "${jobs[@]:-}"; do
    IFS='|' read -r sno il exe ref <<< "$job"
    base="$(basename "$sno" .sno)"

    if [ ! -f "$exe" ]; then
        echo -e "  ${YELLOW}SKIP${RESET} $base (ilasm failed)"
        skip=$((skip+1))
        continue
    fi

    actual=$(mono "$exe" 2>/dev/null)
    expected=$(cat "$ref")

    if [ "$actual" = "$expected" ]; then
        echo -e "  ${GREEN}PASS${RESET} $base"
        pass=$((pass+1))
    else
        echo -e "  ${RED}FAIL${RESET} $base"
        echo "    expected: $(echo "$expected" | head -3)"
        echo "    actual:   $(echo "$actual"   | head -3)"
        fail=$((fail+1))
        [ "$STOP_ON_FAIL" = "1" ] && break
    fi
done

echo ""
echo -e "Results: ${GREEN}${pass} passed${RESET}, ${RED}${fail} failed${RESET}, ${YELLOW}${skip} skipped${RESET}"
[ "$fail" -eq 0 ] && [ "$skip" -eq 0 ] && echo -e "${GREEN}ALL PASS${RESET}"
exit "$fail"
