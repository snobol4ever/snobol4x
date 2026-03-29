#!/usr/bin/env bash
# test/crosscheck/run_crosscheck_asm.sh — Sprint A9: ASM backend crosscheck
#
# For each .sno in crosscheck/patterns/ and crosscheck/capture/:
#   1. Extract subject string (from "X = '...'" line) and pattern (.sno file)
#   2. Compile body-only .s via scrip-cc -asm-body
#   3. Assemble + link with snobol4_asm_harness.o
#   4. Run with subject string as argv[1]
#   5. Compare: for capture tests stdout must match .ref;
#               for match/no-match tests exit code must match expected
#
# Usage: bash test/crosscheck/run_crosscheck_asm.sh [--stop-on-fail]
#
# Environment:
#   SCRIP_CC     path to scrip-cc (default: scrip-cc)
#   HARNESS_O path to harness object (default: src/runtime/asm/snobol4_asm_harness.o)
#   CORPUS    path to crosscheck dir (default: /home/corpus/crosscheck)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"

SCRIP_CC="${SCRIP_CC:-$TINY/scrip-cc}"
HARNESS_O="${HARNESS_O:-src/runtime/asm/snobol4_asm_harness.o}"
CORPUS="${CORPUS:-$(cd "$TINY/../corpus/crosscheck" && pwd)}"

STOP_ON_FAIL=0
for arg in "$@"; do
    [[ "$arg" == "--stop-on-fail" ]] && STOP_ON_FAIL=1
done

# ── Colour helpers ───────────────────────────────────────────────────────────
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
pass() { echo -e "${GREEN}PASS${RESET} $1"; }
fail() { echo -e "${RED}FAIL${RESET} $1"; }
skip() { echo -e "${YELLOW}SKIP${RESET} $1"; }

# ── Working area ─────────────────────────────────────────────────────────────
WORK=/tmp/asm_crosscheck_$$
mkdir -p "$WORK"
trap "rm -rf $WORK" EXIT

passed=0; failed=0; skipped=0

# ── extract_subject: find subject var from match line, then its value ────────
extract_subject() {
    local sno="$1"
    local subj_var
    subj_var=$(perl -ne '
        next if /^\s*\*/;
        next if /^\s*$/;
        next if /^\s+\w+\s*=/;
        next if /^[A-Z]/;
        next if /^END\s*$/;
        if (/^\s+(\w+)\s+\S/) { print $1; exit; }
    ' "$sno")
    [[ -z "$subj_var" ]] && return
    perl -e '
        my $v = $ARGV[0]; my $f = $ARGV[1];
        open my $fh, "<", $f or die;
        while (<$fh>) {
            if (/^\s+\Q$v\E\s*=\s'"'"'([^'"'"']*)'"'"'/ ||
                /^\s+\Q$v\E\s*=\s"([^"]*)"/) { print $1; exit; }
        }
    ' "$subj_var" "$sno"
}

# ── is_capture_test: does the .sno use . or $ capture? ──────────────────────
is_capture_test() {
    grep -qE '\.\s+[A-Z]|\$\s*[A-Z]' "$1"
}

# ── build_bare_sno: produce minimal .sno for scrip-cc -asm-body ─────────────────
build_bare_sno() {
    local sno="$1" out="$2"
    local star_vars
    star_vars=$(perl -ne 'while (/\*([A-Z][A-Z0-9]*)/g){print "$1\n"}' "$sno" \
                | sort -u | paste -sd'|' -)
    perl -e '
        my ($sv, $sno, $out) = @ARGV;
        open my $in,  "<", $sno or die;
        open my $fh,  ">", $out or die;
        while (<$in>) {
            next if /^\s*\*/;
            next if /^\s*$/;
            next if /^END\s*$/;
            next if /^[A-Z][A-Z0-9]*\s+OUTPUT/;
            next if /:\(END\)/;
            if (/^\s+(\w+)\s*=\s*(.*)/) {
                my ($var, $rhs) = ($1, $2);
                if ($rhs =~ /[|(]/ ||
                    $rhs =~ /\b(ARB|ARBNO|ANY|NOTANY|SPAN|BREAK|LEN|POS|RPOS|TAB|RTAB|REM|FAIL)\b/i) {
                    s/\s*:S\([^)]*\)F\([^)]*\)//;
                    s/\s*:S\([^)]*\)//;
                    print $fh $_;
                } elsif ($sv && $var =~ /^($sv)$/) {
                    s/\s*:S\([^)]*\)F\([^)]*\)//;
                    s/\s*:S\([^)]*\)//;
                    print $fh $_;
                }
                next;
            }
            if (/^\s+\w+\s+\S/ || /^\s+\w+\s*$/) {
                s/\s*:S\([^)]*\)F\([^)]*\)//;
                s/\s*:S\([^)]*\)//;
                s/\s*:F\([^)]*\)//;
                print $fh $_;
            }
        }
        print $fh "END\n";
    ' "$star_vars" "$sno" "$out"
}

# ── run_one: process a single .sno file ──────────────────────────────────────
run_one() {
    local sno="$1"
    local ref="${sno%.sno}.asm.ref"
    [[ -f "$ref" ]] || ref="${sno%.sno}.ref"
    local tag
    tag=$(basename "$sno" .sno)

    [[ -f "$ref" ]] || { skip "$tag (no .ref)"; ((skipped++)) || true; return; }

    local subject
    subject=$(extract_subject "$sno")
    if [[ -z "$subject" ]]; then
        skip "$tag (cannot extract subject)"
        ((skipped++)) || true
        return
    fi

    # Build bare .sno
    local bare="$WORK/${tag}_bare.sno"
    build_bare_sno "$sno" "$bare"

    # scrip-cc -asm-body
    local body_s="$WORK/${tag}_body.s"
    if ! "$SCRIP_CC" -asm-body "$bare" > "$body_s" 2>"$WORK/${tag}_scrip-cc.err"; then
        fail "$tag (scrip-cc error: $(cat $WORK/${tag}_scrip-cc.err))"
        ((failed++)) || true
        [[ $STOP_ON_FAIL -eq 1 ]] && exit 1
        return
    fi

    # nasm
    local body_o="$WORK/${tag}_body.o"
    if ! nasm -f elf64 -w-other -I src/runtime/asm/ "$body_s" -o "$body_o" 2>"$WORK/${tag}_nasm.err"; then
        fail "$tag (nasm error: $(cat $WORK/${tag}_nasm.err))"
        ((failed++)) || true
        [[ $STOP_ON_FAIL -eq 1 ]] && exit 1
        return
    fi

    # link
    local bin="$WORK/${tag}_bin"
    if ! gcc -no-pie -o "$bin" "$body_o" "$HARNESS_O" 2>"$WORK/${tag}_ld.err"; then
        fail "$tag (link error: $(cat $WORK/${tag}_ld.err))"
        ((failed++)) || true
        [[ $STOP_ON_FAIL -eq 1 ]] && exit 1
        return
    fi

    # run
    local got_out="$WORK/${tag}_got.txt"
    local exit_code=0
    "$bin" "$subject" > "$got_out" 2>/dev/null || exit_code=$?

    local ref_content
    ref_content=$(cat "$ref")

    if is_capture_test "$sno"; then
        # Capture test: stdout must match ref
        local got_content
        got_content=$(cat "$got_out")
        if [[ "$got_content" == "$ref_content" ]]; then
            pass "$tag"
            ((passed++)) || true
        else
            fail "$tag (stdout='$got_content' expected='$ref_content')"
            ((failed++)) || true
            [[ $STOP_ON_FAIL -eq 1 ]] && exit 1
        fi
    else
        # Match/no-match test: check exit code vs ref
        # ref 'matched' → expect exit 0; ref 'no match' → expect exit 1
        if echo "$ref_content" | grep -qi "no match\|fail"; then
            local expect_exit=1
        else
            local expect_exit=0
        fi
        if [[ $exit_code -eq $expect_exit ]]; then
            pass "$tag"
            ((passed++)) || true
        else
            fail "$tag (exit=$exit_code expected=$expect_exit, subject='$subject')"
            ((failed++)) || true
            [[ $STOP_ON_FAIL -eq 1 ]] && exit 1
        fi
    fi
}

# ── main ─────────────────────────────────────────────────────────────────────
echo "ASM crosscheck — Sprint A9"
echo "SCRIP_CC:    $SCRIP_CC"
echo "HARNESS:  $HARNESS_O"
echo "CORPUS:   $CORPUS"
echo ""

for sno in "$CORPUS"/patterns/*.sno "$CORPUS"/capture/*.sno; do
    [[ -f "$sno" ]] || continue
    run_one "$sno"
done

echo ""
echo "============================================"
echo -e "Results: ${GREEN}${passed} passed${RESET}, ${RED}${failed} failed${RESET}, ${YELLOW}${skipped} skipped${RESET}"
if [[ $failed -eq 0 ]]; then
    echo -e "${GREEN}ALL PASS${RESET}"
    exit 0
else
    echo -e "${RED}FAILURES DETECTED${RESET}"
    exit 1
fi
