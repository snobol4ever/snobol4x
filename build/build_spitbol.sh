#!/usr/bin/env bash
# build_spitbol.sh — build SPITBOL x64 oracle from snobol4ever/x64
# Idempotent. Safe to run multiple times.
# Applies systm.c patch (nanoseconds -> milliseconds) before building.
# Usage: bash build/build_spitbol.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
X64="$ROOT/x64"

[ -d "$X64/.git" ] || { echo "FAIL clone snobol4ever/x64 to $X64 first"; exit 1; }

if [ -x "$X64/bin/sbl" ]; then
    echo "SKIP spitbol already built at $X64/bin/sbl"
else
    # systm.c patch — nanoseconds -> milliseconds (required for correct timing)
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
    cd "$X64" && make 2>&1 | tail -3
    echo "OK  spitbol built: $X64/bin/sbl"
fi

# bootsbl symlink (monitor uses this name)
[ -e "$X64/bin/bootsbl" ] || ln -sf "$X64/bin/sbl" "$X64/bin/bootsbl"

# Smoke test — SPITBOL exits 1 in sandbox (segfault-on-exit) but output is correct
printf "        OUTPUT = 'spitbol-ok'\nEND\n" > /tmp/_build_spitbol_smoke.sno
out=$("$X64/bin/sbl" -b /tmp/_build_spitbol_smoke.sno 2>/dev/null || true)
rm -f /tmp/_build_spitbol_smoke.sno
[ "$out" = "spitbol-ok" ] || { echo "FAIL smoke test (got: $out)"; exit 1; }
echo "OK  spitbol smoke test passed"
echo "OK  binary: $X64/bin/sbl"
