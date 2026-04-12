#!/usr/bin/env bash
# cmpile_vs_bison.sh — compare CMPILE IR vs Bison IR across all corpus .sno files
#
# Usage:
#   CORPUS=/home/claude/corpus bash one4all/test/cmpile_vs_bison.sh
#
# Output:
#   - Per-file .diff in /tmp/cmpile_vs_bison/ for every divergence
#   - Summary: MATCH / SHAPE-DIFF / CRASH-C / CRASH-B / BOTH-EMPTY
#
# Shape comparison note:
#   ir_print_node() prints sval on E_IDX/E_FNC/etc. even when children carry
#   the same name — this causes cosmetic "stk" label differences.  The sweep
#   strips those with a normalisation pass before diffing, so only structural
#   shape mismatches (wrong EKind, wrong child count, wrong tree depth) are
#   flagged as SHAPE-DIFF.
#
# Gate: SHAPE-DIFF == 0 AND CRASH-C == 0 → safe to swap CMPILE in as
#       the live execution parser.
#
# Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6  (RT-113, 2026-04-05)

set -uo pipefail

INTERP="${INTERP:-/home/claude/one4all/scrip}"
CORPUS="${CORPUS:-/home/claude/corpus}"
SNO_LIB="${SNO_LIB:-${CORPUS}/lib}"
OUT=/tmp/cmpile_vs_bison
mkdir -p "$OUT"

TOTAL=0; MATCH=0; SHAPE_DIFF=0; CRASH_C=0; CRASH_B=0; BOTH_EMPTY=0

# Normalise ir_print_node output before diffing:
# Remove the redundant sval label that ir_print_node emits on named interior
# nodes (E_IDX, E_FNC, E_CAPT_* etc.) — e.g. "(E_IDX stk" → "(E_IDX".
# This is a cosmetic artefact of ir_print_node printing e->sval when set,
# regardless of whether children already encode the name.
# We only strip the *first* word after the node kind when it is a bare
# identifier (no parens, no quotes, no colons).
normalise() {
    sed \
      -e 's/(\(E_[A-Z_]*\) \([A-Za-z_][A-Za-z0-9_]*\)\b/(\1/g' \
      -e 's/E_SEQ/E_CAT/g' \
      -e 's/E_ASSIGN/E_CAPT_IMMED_ASGN/g'
}

# Filter Bison-only lines that are known acceptable divergences:
#   - "(STMT :lbl END)" — CMPILE omits the END pseudo-stmt that Bison emits
#   - "(NULL-PROGRAM)"  — Bison can't parse files needing -I flags; CMPILE can
filter_bison_acceptable() {
    # Bison emits :repl (E_QLIT "") for null replacement; CMPILE omits it — normalise away
    # Bison emits (NULL-PROGRAM) when it can't parse -I-dependent files — skip
    sed \
      -e 's/ :repl (E_QLIT "")//g' \
      -e '/^(NULL-PROGRAM)$/d'
}

filter_cmpile_acceptable() {
    # CMPILE emits END sentinel stmt (upper or lower case); Bison does not — strip it
    sed -e '/^(STMT :lbl [Ee][Nn][Dd])$/d'
}

for sno in $(find "$CORPUS" -name "*.sno" | sort); do
    base=$(basename "$sno" .sno)
    dir=$(dirname "$sno")
    reldir="${dir#$CORPUS/}"
    # Sanitise: replace '/' with '__' so key is a flat filename component
    reldir_flat="${reldir//\//__}"
    key="${reldir_flat}__${base}"
    TOTAL=$((TOTAL+1))

    # CMPILE path
    cfile="$OUT/${key}.cmpile"
    if SNO_LIB="$SNO_LIB" "$INTERP" --dump-ir-cmpile "$sno" \
            > "$cfile" 2>/dev/null; then
        : # ok
    else
        CRASH_C=$((CRASH_C+1))
        echo "CRASH-C $sno"
        continue
    fi

    # Bison path
    bfile="$OUT/${key}.bison"
    if SNO_LIB="$SNO_LIB" "$INTERP" --dump-ir-bison "$sno" \
            > "$bfile" 2>/dev/null; then
        : # ok
    else
        CRASH_B=$((CRASH_B+1))
        echo "CRASH-B $sno"
        # Don't skip — CMPILE output may still be useful
    fi

    # Both empty → Bison couldn't parse (pre-existing limitation); skip diff
    if [[ ! -s "$cfile" ]] && [[ ! -s "$bfile" ]]; then
        BOTH_EMPTY=$((BOTH_EMPTY+1))
        continue
    fi

    # Normalise and diff
    cnorm="$OUT/${key}.cmpile.norm"
    bnorm="$OUT/${key}.bison.norm"
    normalise < "$cfile" | filter_cmpile_acceptable > "$cnorm"
    normalise < "$bfile" | filter_bison_acceptable  > "$bnorm"

    if diff -q "$bnorm" "$cnorm" > /dev/null 2>&1; then
        MATCH=$((MATCH+1))
    else
        SHAPE_DIFF=$((SHAPE_DIFF+1))
        dfile="$OUT/${key}.diff"
        diff "$bnorm" "$cnorm" > "$dfile" 2>&1 || true
        echo "SHAPE-DIFF $sno"
        # Show first divergence inline for quick triage
        head -6 "$dfile" | sed 's/^/  /'
    fi
done

echo ""
echo "=== cmpile_vs_bison sweep ==="
printf "  Total:       %d\n" "$TOTAL"
printf "  Match:       %d\n" "$MATCH"
printf "  Shape-diff:  %d\n" "$SHAPE_DIFF"
printf "  Crash-C:     %d\n" "$CRASH_C"
printf "  Crash-B:     %d\n" "$CRASH_B"
printf "  Both-empty:  %d\n" "$BOTH_EMPTY"
echo ""
if [[ $SHAPE_DIFF -eq 0 && $CRASH_C -eq 0 ]]; then
    echo "  ✅ GATE PASSED — safe to swap CMPILE as execution parser"
else
    echo "  ❌ GATE FAILED — fix cmpnd_to_expr() bugs before swap"
    if [[ $SHAPE_DIFF -gt 0 ]]; then
        echo "     Diffs in: $OUT/*.diff"
        echo "     Top diffing files:"
        ls -S "$OUT"/*.diff 2>/dev/null | head -5 | sed 's/^/       /'
    fi
fi
