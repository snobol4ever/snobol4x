#!/usr/bin/env bash
# test_smoke_rebus.sh — per-frontend smoke for Rebus  (FI-9)
# Gate: exits 0 in < 2s on a clean build.
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6  DATE: 2026-04-14
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
PASS=0; FAIL=0

rebus() {
    local label="$1" expected="$2"
    local tmp; tmp=$(mktemp /tmp/reb_XXXXXX.reb)
    cat > "$tmp"
    local actual; actual=$(timeout 8 "$SCRIP" --ir-run "$tmp" 2>/dev/null)
    rm -f "$tmp"
    if [ "$actual" = "$expected" ]; then echo "  PASS $label"; PASS=$((PASS+1))
    else echo "  FAIL $label (got: $(echo "$actual"|head -1))"; FAIL=$((FAIL+1)); fi
}

echo "=== Rebus smoke ==="

rebus "output_str" "hello" << 'EOF'
function main()
  OUTPUT := "hello"
end
EOF

rebus "arith" "7" << 'EOF'
function main()
  OUTPUT := 3 + 4
end
EOF

rebus "var" "42" << 'EOF'
function main()
  x := 42
  OUTPUT := x
end
EOF

rebus "concat" "abcd" << 'EOF'
function main()
  OUTPUT := "ab" || "cd"
end
EOF

echo ""; echo "PASS=$PASS FAIL=$FAIL"; [ "$FAIL" -eq 0 ]
