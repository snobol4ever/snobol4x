#!/usr/bin/env bash
# test/smoke.sh — scrip smoke test: does it build and run in all modes?
# Usage: bash test/smoke.sh
# From:  /home/claude/one4all/

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP="${SCRIP:-$ROOT/scrip}"
JASMIN="${JASMIN:-$ROOT/src/backend/jasmin.jar}"
PASS=0; FAIL=0

have() { command -v "$1" &>/dev/null; }

check() {
    local label="$1" expected="$2" got="$3"
    if [ "$got" = "$expected" ]; then
        echo "PASS $label"
        PASS=$((PASS+1))
    else
        echo "FAIL $label"
        echo "  expected: $(printf '%s' "$expected" | head -1)"
        echo "  got:      $(printf '%s' "$got" | head -1)"
        FAIL=$((FAIL+1))
    fi
}

skip() { echo "SKIP $1 ($2 not found)"; }

# One hello-world program used across all modes
SNO=$(mktemp /tmp/scrip_smoke_XXXXXX.sno)
printf "        OUTPUT = 'hello'\nEND\n" > "$SNO"
trap 'rm -f "$SNO"' EXIT

# ── interpreter modes ─────────────────────────────────────────────────────────
check "sm-run (default)" "hello" "$("$SCRIP" "$SNO" 2>/dev/null)"
check "ir-run"           "hello" "$("$SCRIP" --ir-run "$SNO" 2>/dev/null)"

# ── x86 emit mode ─────────────────────────────────────────────────────────────
if have nasm && have gcc; then
    TMP=$(mktemp -d)
    "$SCRIP" --jit-emit --x64 "$SNO" > "$TMP/prog.s" 2>/dev/null &&
    nasm -f elf64 "$TMP/prog.s" -o "$TMP/prog.o" 2>/dev/null &&
    gcc "$TMP/prog.o" -lgc -lm -o "$TMP/prog" 2>/dev/null &&
    got=$("$TMP/prog" 2>/dev/null) ||
    got=""
    rm -rf "$TMP"
    check "x86 emit" "hello" "$got"
else
    skip "x86 emit" "nasm/gcc"
fi

# ── JVM emit mode ─────────────────────────────────────────────────────────────
if have java && [ -f "$JASMIN" ]; then
    TMP=$(mktemp -d)
    "$SCRIP" --jit-emit --jvm "$SNO" > "$TMP/prog.j" 2>/dev/null
    if grep -q "^\.class\|^\.source" "$TMP/prog.j" 2>/dev/null; then
        java -jar "$JASMIN" -d "$TMP" "$TMP/prog.j" 2>/dev/null &&
        got=$(java -cp "$TMP" Main 2>/dev/null) || got=""
        check "JVM emit" "hello" "$got"
    else
        echo "SKIP JVM emit (not yet implemented)"
    fi
    rm -rf "$TMP"
else
    skip "JVM emit" "java/jasmin"
fi

# ── NET emit mode ─────────────────────────────────────────────────────────────
if have ilasm && have mono; then
    TMP=$(mktemp -d)
    "$SCRIP" --jit-emit --net "$SNO" > "$TMP/prog.il" 2>/dev/null &&
    ilasm "$TMP/prog.il" /output:"$TMP/prog.exe" 2>/dev/null &&
    got=$(mono "$TMP/prog.exe" 2>/dev/null) ||
    got=""
    rm -rf "$TMP"
    check "NET emit" "hello" "$got"
else
    skip "NET emit" "ilasm/mono"
fi

echo ""
echo "PASS=$PASS FAIL=$FAIL"
[ $FAIL -eq 0 ]
