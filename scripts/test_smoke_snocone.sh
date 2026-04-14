#!/usr/bin/env bash
# test_smoke_snocone.sh — per-frontend smoke for Snocone  (FI-9)
# Gate: exits 0 in < 2s on a clean build.
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6  DATE: 2026-04-14
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
PASS=0; FAIL=0

snocone() {
    local label="$1" expected="$2"
    local tmp; tmp=$(mktemp /tmp/sc_XXXXXX.sc)
    cat > "$tmp"
    local actual; actual=$(timeout 8 "$SCRIP" --ir-run "$tmp" 2>/dev/null)
    rm -f "$tmp"
    if [ "$actual" = "$expected" ]; then echo "  PASS $label"; PASS=$((PASS+1))
    else echo "  FAIL $label (got: $(echo "$actual"|head -1))"; FAIL=$((FAIL+1)); fi
}

echo "=== Snocone smoke ==="

snocone "output" "hello" << 'EOF'
OUTPUT = "hello"
EOF

snocone "arith" "5" << 'EOF'
OUTPUT = 2 + 3
EOF

snocone "procedure" "42" << 'EOF'
procedure Double(n) {
    Double = n + n; return;
}
OUTPUT = Double(21)
EOF

snocone "if_eq" "yes" << 'EOF'
if (EQ(2 + 2, 4)) { OUTPUT = "yes"; } else { OUTPUT = "no"; }
EOF

snocone "while" "$(printf '1\n2\n3')" << 'EOF'
i = 1;
while (LE(i, 3)) { OUTPUT = i; i = i + 1; }
EOF

echo ""; echo "PASS=$PASS FAIL=$FAIL"; [ "$FAIL" -eq 0 ]
