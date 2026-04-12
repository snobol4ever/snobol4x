#!/usr/bin/env bash
# build_csnobol4.sh — build CSNOBOL4 from snobol4ever/csnobol4
# Idempotent. Safe to run multiple times.
# All patches (FENCE(P), STNO trace) are baked into the committed source.
# Usage: bash build/build_csnobol4.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPO="$ROOT/csnobol4"

[ -d "$REPO/.git" ] || { echo "FAIL clone snobol4ever/csnobol4 to $REPO first"; exit 1; }
apt-get install -y m4 -qq 2>/dev/null || true

cd "$REPO"
make -f Makefile2 xsnobol4 -j4
cp "$REPO/xsnobol4" "$REPO/snobol4"

# Smoke test
printf "        OUTPUT = 'csnobol4-ok'\nEND\n" > /tmp/_build_csnobol4_smoke.sno
out=$("$REPO/snobol4" /tmp/_build_csnobol4_smoke.sno 2>/dev/null)
rm -f /tmp/_build_csnobol4_smoke.sno
[ "$out" = "csnobol4-ok" ] || { echo "FAIL smoke test (got: $out)"; exit 1; }
echo "OK  csnobol4 smoke test passed"
echo "OK  binary: $REPO/snobol4"
