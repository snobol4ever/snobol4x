#!/usr/bin/env python3
"""
oracle_sprint18.py — Sprint 18 Oracle: The Full Round-Trip

The self-hosting moment.

Generate an expression with gen_term() or rand_expression().
Evaluate it TWO ways:
  1. Python:   Expressions.py parse_expression() + evaluate()
  2. Compiled: build_expr_evaluator binary (C, via value stack)

If they agree — the compiled evaluator is correct.
If they disagree — the worm found a bug.

This is the cross-check that proves the compiler can replace
its own reference implementation.

580 Python reference cases (Sprint 15) +
609 parser cases (Sprint 16) +
This: evaluate round-trip.

When this passes — Sprint 18 is done. The self-hosting moment.
"""

import sys, os, subprocess, tempfile, random

HERE    = os.path.dirname(os.path.abspath(__file__))
ROOT    = os.path.join(HERE, '..', '..')

sys.path.insert(0, os.path.join(HERE, '..', 'sprint15'))
sys.path.insert(0, HERE)

from build_expr_evaluator import build_evaluator

# Load Expressions.py
_ns  = {}
_src = open(os.path.join(HERE, '..', 'sprint15', 'Expressions.py')).read()
_src = _src[:_src.index("import sys")]
exec(_src, _ns)

gen_term        = _ns['gen_term']
rand_expression = _ns['rand_expression']
parse_expression = _ns['parse_expression']
evaluate        = _ns['evaluate']

def python_eval(expr_str):
    _ns['pos'] = 0; _ns['subject'] = expr_str
    try:
        tree = next(parse_expression())
        return evaluate(tree)
    except StopIteration:
        return None

# ---------------------------------------------------------------------------
print("=== Sprint 18 Oracle — The Full Round-Trip ===")
print()
print("Building compiled evaluator...")

with tempfile.NamedTemporaryFile(suffix='', delete=False) as f:
    bin_path = f.name
binary = build_evaluator(out_path=bin_path)
if not binary:
    sys.exit(1)
print(f"  Built: {binary}")
print()

passed = total = 0

def check(desc, expr_str):
    global passed, total
    total += 1

    ref = python_eval(expr_str)
    if ref is None:
        # Python reference couldn't parse — skip
        return

    r = subprocess.run([binary, expr_str], capture_output=True, text=True)
    if r.returncode != 0:
        print(f"[FAIL] {desc}: {expr_str!r}")
        print(f"       compiled: PARSE ERROR — {r.stderr.strip()}")
        print(f"       python:   {ref}")
        return

    try:
        got = int(r.stdout.strip())
    except ValueError:
        print(f"[FAIL] {desc}: {expr_str!r} — bad output {r.stdout!r}")
        return

    if got == ref:
        passed += 1
    else:
        print(f"[FAIL] {desc}: {expr_str!r}")
        print(f"       compiled: {got}")
        print(f"       python:   {ref}")

# Phase 1: growing-token systematic
print("Phase 1: gen_term() systematic (N=100)")
g = gen_term()
lens = []
for i in range(100):
    expr = next(g)
    lens.append(len(expr))
    check(f"gen[{i+1:3d}] len={len(expr):4d}", expr)
print(f"  lengths: 1..{max(lens)}  |  {passed}/{total} passed")
print()

# Phase 2: random worm
print("Phase 2: rand_expression() random worm (N=500)")
random.seed(42)
p0, t0 = passed, total
for i in range(500):
    check(f"rand[{i:3d}]", rand_expression())
print(f"  {passed-p0}/{total-t0} passed")
print()

# Phase 3: specific arithmetic cases
print("Phase 3: arithmetic correctness cases")
cases = [
    ('x',           10),
    ('y',           20),
    ('z',           30),
    ('42',          42),
    ('x+y',         30),
    ('x+y+z',       60),
    ('x*y*z',     6000),
    ('x+y*z',      610),
    ('(x+y)*z',    900),
    ('-x',         -10),
    ('+y',          20),
    ('--z',         30),
    ('x-y',        -10),
    ('z/x',          3),
    ('x/y',          0),   # integer division
    ('x/z',          0),   # 10//30 = 0
    ('z/y',          1),   # 30//20 = 1
    ('x*y+z',      230),   # 200+30
    ('x+y-z',        0),   # 10+20-30
    ('(x+y)*(y+z)', 1250), # 30*50 -- wait: 30*(20+30)=30*50=1500? No: (10+20)*(20+30)=30*50=1500
]
# Correct the last: (x+y)*(y+z) = 30*50 = 1500
cases[-1] = ('(x+y)*(y+z)', 1500)

p0, t0 = passed, total
for expr, expected in cases:
    ref = python_eval(expr)
    r = subprocess.run([binary, expr], capture_output=True, text=True)
    got = int(r.stdout.strip()) if r.returncode == 0 else None
    total += 1
    if got == expected and ref == expected:
        passed += 1
    else:
        print(f"[FAIL] {expr!r}: compiled={got}, python={ref}, expected={expected}")
print(f"  {passed-p0}/{total-t0} passed")
print()

# Cleanup
os.unlink(binary)

# Summary
print("=================================================================")
print(f"  TOTAL: {passed}/{total} passed")
if passed == total:
    print()
    print("  THE WORM FOUND NOTHING.")
    print()
    print("  The compiled evaluator agrees with the Python reference")
    print("  on every generated expression.")
    print()
    print("  This is the self-hosting moment:")
    print("  The compiler can replace its own reference implementation.")
    print()
    print("  Sprint 19: EVAL — the hard one.")
else:
    print(f"  {total-passed} FAILURES. The worm works.")
print("=================================================================")
sys.exit(0 if passed == total else 1)
