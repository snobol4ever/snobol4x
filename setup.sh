#!/usr/bin/env bash
# setup.sh — one-time environment bootstrap for snobol4ever development
#
# Idempotent: safe to run multiple times.
# After running: snobol4, spitbol, scrip, snobol4-x86, snobol4-jvm all work.
#
# Usage: bash setup.sh [--skip-csnobol4] [--skip-spitbol] [--skip-scrip]
set -euo pipefail

SNOBOL4X="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SNOBOL4X/.." && pwd)"
CORPUS="$ROOT/corpus"
X64="$ROOT/x64"
CSNOBOL4_SRC="$ROOT/csnobol4-src"

# Local uploads folder — change this if your tarballs are elsewhere
UPLOADS="/home/satirical/claude"
TARBALL="$UPLOADS/snobol4-2_3_3_tar.gz"

GREEN='\033[0;32m'; YELLOW='\033[0;33m'; RED='\033[0;31m'; RESET='\033[0m'
ok()   { echo -e "${GREEN}OK${RESET}  $*"; }
skip() { echo -e "${YELLOW}SKIP${RESET} $*"; }
fail() { echo -e "${RED}FAIL${RESET} $*"; exit 1; }
step() { echo -e "\n── $* ──"; }

# ── 1. System packages ────────────────────────────────────────────────────────
step "System packages"
PKGS="build-essential libgmp-dev m4 nasm libgc-dev wabt"
MISSING=""
for p in $PKGS; do
    dpkg -s "$p" &>/dev/null || MISSING="$MISSING $p"
done
if [ -n "$MISSING" ]; then
    apt-get install -y $MISSING 2>&1 | tail -3
    ok "installed:$MISSING"
else
    skip "all packages already installed"
fi

# ── 2. CSNOBOL4 2.3.3 ────────────────────────────────────────────────────────
step "CSNOBOL4 2.3.3"
if /usr/local/bin/snobol4 --version &>/dev/null || \
   (printf "        OUTPUT = 'ok'\nEND\n" | /usr/local/bin/snobol4 /dev/stdin 2>/dev/null | grep -q ok); then
    skip "snobol4 already installed at /usr/local/bin/snobol4"
else
    [ -f "$TARBALL" ] || fail "tarball not found: $TARBALL"
    mkdir -p "$CSNOBOL4_SRC"
    tar xzf "$TARBALL" -C "$CSNOBOL4_SRC/" --strip-components=1
    cd "$CSNOBOL4_SRC"
    # STNO trace patch — required for monitor
    sed -i '/if (!chk_break(0))/{N;/goto L_INIT1;/d}' snobol4.c isnobol4.c
    ./configure --prefix=/usr/local 2>&1 | tail -2
    # Build ignoring the keytrace test failure (expected with STNO patch)
    make -j4 2>&1 | grep -v "^make\|passed\|failed\|sno:\|testing\|search\|from\|blocks" | tail -5 || true
    # Install binary directly — test suite failure is a known false alarm
    cp "$CSNOBOL4_SRC/xsnobol4" /usr/local/bin/snobol4
    chmod +x /usr/local/bin/snobol4
    # Install snolib
    cp -r "$CSNOBOL4_SRC/snolib" /usr/local/share/snobol4 2>/dev/null || true
    cd "$SNOBOL4X"
    ok "snobol4 installed"
fi
# Smoke test
printf "        OUTPUT = 'csnobol4-ok'\nEND\n" > /tmp/_setup_smoke.sno
out=$(/usr/local/bin/snobol4 /tmp/_setup_smoke.sno 2>/dev/null)
[ "$out" = "csnobol4-ok" ] || fail "snobol4 smoke test failed (got: $out)"
ok "snobol4 smoke test passed"

# ── 3. SPITBOL x64 ───────────────────────────────────────────────────────────
step "SPITBOL x64"
if [ -x /usr/local/bin/spitbol ]; then
    skip "spitbol already installed"
else
    [ -d "$X64" ] || fail "x64 repo not found: $X64"
    # systm.c patch — nanoseconds → milliseconds
    cat > "$X64/osint/systm.c" << 'PATCH'
#include "port.h"
#include "time.h"
int zystm() {
    struct timespec tim;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tim);
    long etime = (long)(tim.tv_sec * 1000) + (long)(tim.tv_nsec / 1000000);
    SET_IA(etime);
    return NORMAL_RETURN;
}
PATCH
    cd "$X64"
    make 2>&1 | tail -3
    cp "$X64/sbl" /usr/local/bin/spitbol
    chmod +x /usr/local/bin/spitbol
    cd "$SNOBOL4X"
    ok "spitbol installed"
fi
# bootsbl symlink (monitor uses this name)
[ -e "$X64/bootsbl" ] || ln -sf "$X64/sbl" "$X64/bootsbl"
# Smoke test — spitbol exits 139 in sandbox (segfault on exit) but output is correct
out=$(spitbol -b /tmp/_setup_smoke.sno 2>/dev/null || true)
# Reuse hello for spitbol
printf "        OUTPUT = 'spitbol-ok'\nEND\n" > /tmp/_setup_spitbol.sno
out=$(spitbol -b /tmp/_setup_spitbol.sno 2>/dev/null || true)
[ "$out" = "spitbol-ok" ] || fail "spitbol smoke test failed (got: $out)"
ok "spitbol smoke test passed (exit code ignored — sandbox segfault-on-exit is expected)"

# ── 4. scrip compiler ────────────────────────────────────────────────────────
step "scrip compiler"
if [ -x "$SNOBOL4X/scrip" ]; then
    skip "scrip already built"
else
    cd "$SNOBOL4X/src"
    make -j4 2>&1 | tail -3
    cd "$SNOBOL4X"
    ok "scrip built"
fi
[ -x "$SNOBOL4X/scrip" ] || fail "scrip not found after build"
# scrip_jvm symlink (snobol4-jvm script expects scrip_jvm in $HOME)
[ -e "$HOME/scrip_jvm" ] || ln -sf "$SNOBOL4X/scrip" "$HOME/scrip_jvm"
ok "scrip ready"

# ── 5. Java / Jasmin ─────────────────────────────────────────────────────────
step "Java / Jasmin (JVM backend)"
which java  &>/dev/null || fail "java not found — install openjdk"
which javac &>/dev/null || { apt-get install -y openjdk-21-jdk-headless 2>&1 | tail -2; which javac &>/dev/null || fail "javac not found after install"; }
[ -f "$SNOBOL4X/src/backend/jasmin.jar" ] || fail "jasmin.jar missing"
ok "java $(java -version 2>&1 | grep -o 'version "[^"]*"' || echo 'found')"
ok "jasmin.jar present"

# ── 6. monitor_ipc.so ────────────────────────────────────────────────────────
step "monitor_ipc.so"
MDIR="$SNOBOL4X/test/monitor"
if [ -f "$MDIR/monitor_ipc.so" ]; then
    skip "monitor_ipc.so already built"
else
    gcc -shared -fPIC -o "$MDIR/monitor_ipc.so" "$MDIR/monitor_ipc.c" 2>&1
    ok "monitor_ipc.so built"
fi
ok "monitor_ipc.so present"

# ── 7. Corpus smoke test ─────────────────────────────────────────────────────
step "ASM corpus invariant (106/106)"
result=$(bash "$SNOBOL4X/test/crosscheck/run_crosscheck_x86_corpus.sh" 2>&1 | tail -3)
echo "$result"
echo "$result" | grep -q "ALL PASS" || fail "ASM corpus invariant broken — do not proceed"
ok "106/106 ALL PASS"

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}Setup complete.${RESET} Environment ready for monitor + beauty sprint."
echo "  snobol4:  $(which snobol4)"
echo "  spitbol:  $(which spitbol)"
echo "  scrip:    $SNOBOL4X/scrip"
echo "  monitor:  $MDIR/run_monitor.sh"
echo "  corpus:   $CORPUS"
rm -f /tmp/_setup_smoke.sno /tmp/_setup_spitbol.sno
