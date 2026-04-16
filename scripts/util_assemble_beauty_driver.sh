#!/usr/bin/env bash
# util_assemble_beauty_driver.sh — assemble test/beauty-sc/beauty/driver.sc
# from all subsystem library files, deduplicating struct declarations.
#
# Usage:
#   bash scripts/util_assemble_beauty_driver.sh [--output PATH] [--dry-run]
#
#   --output PATH   write assembled driver to PATH (default: test/beauty-sc/beauty/driver.sc)
#   --dry-run       print assembled content to stdout, do not write file
#
# Order matches beauty.sno -INCLUDE order:
#   global, case, io, assign, match, counter, stack, tree,
#   ShiftReduce, Gen, Qize, ReadWrite, TDump, XDump, semantic, omega, trace
# then beauty.sc main body.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BDIR="$ROOT/test/beauty-sc"
DEFAULT_OUT="$BDIR/beauty/driver.sc"

OUTPUT_PATH="$DEFAULT_OUT"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output) OUTPUT_PATH="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# extract_lib FILE
#   Emit only the library portion of a driver.sc — everything before the first
#   &STLIMIT or "// N:" test-marker line, minus the file header comment.
# ---------------------------------------------------------------------------
extract_lib() {
    local f="$1"
    sed -n '1,/^&STLIMIT\|^\/\/ [0-9]\+:/p' "$f" \
        | grep -v '^&STLIMIT\|^\/\/ [0-9]\+:\|^\/\/ driver\|^\/\/ Oracle\|^\/\/ inline'
}

# strip_structs — remove shared struct declarations (declared once in preamble)
strip_structs() {
    grep -v '^struct link\b\|^struct tree\b'
}

# strip_stack_procs — remove stack procedure bodies already emitted from stack section.
# ShiftReduce/driver.sc duplicates InitStack/Push/Pop/Top from stack/driver.sc.
# Uses awk to skip entire multi-line procedure blocks.
strip_stack_procs() {
    awk '
        /^procedure (InitStack|Push|Pop|Top)\b/ { skip=1; depth=0 }
        skip {
            for (i=1; i<=length($0); i++) {
                c = substr($0,i,1)
                if (c == "{") depth++
                else if (c == "}") { depth--; if (depth==0) { skip=0; next } }
            }
            next
        }
        { print }
    '
}

assemble() {
    echo "// driver.sc — combined beauty subsystem driver"
    echo "// Assembled by util_assemble_beauty_driver.sh — do not edit by hand."
    echo "// Source order: global case io assign match counter stack tree"
    echo "//               ShiftReduce Gen Qize ReadWrite TDump XDump semantic omega trace"
    echo "//               beauty (main body)"
    echo ""

    echo "// === shared struct declarations ==="
    echo "struct link { next, value }"
    echo "struct tree { t, v, n, c }"
    echo ""

    # Required globals before any pattern building
    echo "// === required globals ==="
    echo "xTrace      = 0;"
    echo "t8Max       = 0;"
    echo "txOfs       = 0;"
    echo "tz          = '';"
    echo "tx          = '';"
    echo "dummy       = '';"
    echo "doParseTree = 0;"  # FALSE not yet defined; use literal 0
    echo "epsilon     = '';"
    echo ""

    for section in \
        "global:$BDIR/global/driver.sc" \
        "case:$BDIR/beauty/case.sc" \
        "io:$BDIR/beauty/io.sc" \
        "assign:$BDIR/assign/driver.sc" \
        "match:$BDIR/match/driver.sc" \
        "counter:$BDIR/counter/driver.sc" \
        "stack:$BDIR/stack/driver.sc" \
        "tree:$BDIR/tree/driver.sc" \
        "ShiftReduce:$BDIR/ShiftReduce/driver.sc" \
        "Gen:$BDIR/beauty/Gen.sc" \
        "Qize:$BDIR/beauty/Qize.sc" \
        "ReadWrite:$BDIR/ReadWrite/driver.sc" \
        "TDump:$BDIR/beauty/TDump.sc" \
        "XDump:$BDIR/beauty/XDump.sc" \
        "semantic:$BDIR/semantic/driver.sc" \
        "omega:$BDIR/beauty/omega.sc" \
        "trace:$BDIR/trace/driver.sc"
    do
        name="${section%%:*}"
        path="${section#*:}"
        echo "// === $name ==="
        if [[ ! -f "$path" ]]; then
            echo "ERROR: missing $path" >&2; exit 1
        fi
        # driver.sc files: extract library portion; .sc files: emit whole file minus header
        if [[ "$path" == */driver.sc ]]; then
            if [[ "$name" == "ShiftReduce" ]]; then
                extract_lib "$path" | strip_structs | strip_stack_procs | grep -v '^// '"$name"
            else
                extract_lib "$path" | strip_structs | grep -v '^// '"$name"
            fi
        else
            grep -v '^// '"$name" "$path" | strip_structs
        fi
        echo ""
    done

    echo "// === beauty main body ==="
    grep -v '^// beauty\|^// Library\|^// separate\|^// Library proc' \
        "$BDIR/beauty/beauty.sc" | strip_structs
}

if [[ "$DRY_RUN" -eq 1 ]]; then
    assemble
else
    assemble > "$OUTPUT_PATH"
    echo "OK  assembled: $OUTPUT_PATH ($(wc -l < "$OUTPUT_PATH") lines)"
fi
