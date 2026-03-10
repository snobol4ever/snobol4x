#!/usr/bin/env python3
"""
oracle_sprint14.py — Sprint 14 oracle

Verifies the snoc compiler end-to-end on the first SNOBOL4 programs.
Each test: compile a .sno file, capture stdout, compare to expected.

This is the first oracle that proves SNOBOL4-tiny is a compiler —
not just a pattern engine.
"""

import subprocess
import sys
import os
import tempfile

HERE  = os.path.dirname(os.path.abspath(__file__))
ROOT  = os.path.join(HERE, '..', '..')
SNOC  = os.path.join(ROOT, 'snoc')

passed = 0
failed = 0

def run_sno(source):
    """Write source to a temp file, compile + run, return stdout."""
    with tempfile.NamedTemporaryFile(suffix='.sno', mode='w',
                                     delete=False) as f:
        f.write(source)
        path = f.name
    result = subprocess.run(
        ['python3', SNOC, path],
        capture_output=True, text=True
    )
    os.unlink(path)
    return result.stdout, result.returncode

def check(description, source, expected_output, expected_rc=0):
    global passed, failed
    stdout, rc = run_sno(source)
    ok = (stdout == expected_output and rc == expected_rc)
    status = "Success!" if ok else "Failure."
    print(f"[{['FAIL','PASS'][ok]}] {description}")
    if not ok:
        print(f"       expected stdout: {expected_output!r}")
        print(f"       got    stdout: {stdout!r}")
        print(f"       expected rc: {expected_rc}, got: {rc}")
        failed += 1
    else:
        passed += 1

print("=== Sprint 14 Oracle — First SNOBOL4 Programs ===")
print()

# ── Test 1: Hello World ────────────────────────────────────────────────────
check(
    "Hello World",
    "OUTPUT = 'HELLO WORLD'\nEND\n",
    "HELLO WORLD\n"
)

# ── Test 2: Empty string ───────────────────────────────────────────────────
check(
    "Empty string output",
    "OUTPUT = ''\nEND\n",
    "\n"
)

# ── Test 3: Multiple output statements ────────────────────────────────────
check(
    "Multiple OUTPUT statements",
    "OUTPUT = 'ONE'\nOUTPUT = 'TWO'\nOUTPUT = 'THREE'\nEND\n",
    "ONE\nTWO\nTHREE\n"
)

# ── Test 4: String with spaces and punctuation ─────────────────────────────
check(
    "String with spaces and punctuation",
    "OUTPUT = 'Hello, World!'\nEND\n",
    "Hello, World!\n"
)

# ── Test 5: Single character ───────────────────────────────────────────────
check(
    "Single character",
    "OUTPUT = 'X'\nEND\n",
    "X\n"
)

# ── Test 6: Comment lines ignored ─────────────────────────────────────────
check(
    "Comment lines are ignored",
    "* This is a comment\nOUTPUT = 'AFTER COMMENT'\nEND\n",
    "AFTER COMMENT\n"
)

# ── Test 7: Classic SNOBOL4 greeting ──────────────────────────────────────
check(
    "Classic SNOBOL4 greeting",
    "OUTPUT = 'THIS IS SNOBOL4'\nEND\n",
    "THIS IS SNOBOL4\n"
)

print()
print(f"=== {passed} passed, {failed} failed ===")
sys.exit(0 if failed == 0 else 1)
