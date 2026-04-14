#!/usr/bin/env bash
# test_smoke_icon.sh — per-frontend smoke for Icon  (FI-9)
# Gate: exits 0 in < 2s on a clean build.
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6  DATE: 2026-04-14
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
PASS=0; FAIL=0

icon() {
    local label="$1" expected="$2"
    local tmp; tmp=$(mktemp /tmp/icn_XXXXXX.icn)
    cat > "$tmp"
    local actual; actual=$(timeout 8 "$SCRIP" --ir-run "$tmp" 2>/dev/null)
    rm -f "$tmp"
    if [ "$actual" = "$expected" ]; then echo "  PASS $label"; PASS=$((PASS+1))
    else echo "  FAIL $label (got: $(echo "$actual"|head -1))"; FAIL=$((FAIL+1)); fi
}

echo "=== Icon smoke ==="

icon "write_str" "hello" << 'EOF'
procedure main()
  write("hello");
end
EOF

icon "arith" "5" << 'EOF'
procedure main()
  write(2 + 3);
end
EOF

icon "string_op" "abcd" << 'EOF'
procedure main()
  write("ab" || "cd");
end
EOF

icon "if_expr" "big" << 'EOF'
procedure main()
  x := 10;
  if x > 5 then write("big"); else write("small");
end
EOF

icon "every" "$(printf '1\n2\n3')" << 'EOF'
procedure main()
  every write(1 to 3);
end
EOF

echo ""; echo "PASS=$PASS FAIL=$FAIL"; [ "$FAIL" -eq 0 ]
