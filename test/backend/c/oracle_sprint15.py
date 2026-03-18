#!/usr/bin/env python3
"""
oracle_sprint15.py — Sprint 15 Oracle: The Worm

Expressions.py is the reference implementation AND the test generator.
It is its own oracle.

The worm technique (Lon Cherryholmes, Pick Systems / FlashBASIC, ~1992):
  Generate a valid input. Parse it. Evaluate it.
  If it fails, you found a bug. Run forever. Never bored.

Phase 1a: gen_term() systematic growing-token coverage (50 cases)
Phase 1b: rand_expression() random worm (500 cases)
Phase 2:  snoc round-trip — OUTPUT = 'expr' through the compiler (30 cases)
Phase 3 (Sprint 16): compile the expression parser itself in SNOBOL4
"""

import sys, os, subprocess, tempfile, random

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
SNOC = os.path.join(ROOT, 'snoc')

# Load Expressions.py as oracle
_ns  = {}
_src = open(os.path.join(HERE, 'Expressions.py')).read()
_src = _src[:_src.index("import sys")]
exec(_src, _ns)

rand_expression  = _ns['rand_expression']
gen_term         = _ns['gen_term']
parse_expression = _ns['parse_expression']
evaluate         = _ns['evaluate']

def ref_eval(expr_str):
    _ns['pos'] = 0; _ns['subject'] = expr_str
    try:
        tree = next(parse_expression())
        return evaluate(tree), True
    except StopIteration:
        return None, False

passed = total = 0

def check(desc, expr_str):
    global passed, total
    total += 1
    val, ok = ref_eval(expr_str)
    if not ok:
        print(f"[FAIL] {desc}: parse failed on {expr_str!r}")
        return False
    passed += 1
    return True

print("=== Sprint 15 Oracle — The Worm ===")
print()

# Phase 1a
print("Phase 1a: gen_term() systematic (N=50)")
g = gen_term()
lens = []
for i in range(50):
    expr = next(g)
    lens.append(len(expr))
    check(f"gen[{i+1}] len={len(expr)}", expr)
print(f"  lengths: 1..{max(lens)}  |  {passed}/{total} passed")
print()

# Phase 1b
print("Phase 1b: rand_expression() random worm (N=500)")
random.seed(42)
p0, t0 = passed, total
for i in range(500):
    check(f"rand[{i}]", rand_expression())
print(f"  {passed-p0}/{total-t0} passed")
print()

# Phase 2
print("Phase 2: snoc round-trip (N=30 growing-token)")
def snoc_run(src):
    with tempfile.NamedTemporaryFile(suffix='.sno', mode='w', delete=False) as f:
        f.write(src); path = f.name
    r = subprocess.run(['python3', SNOC, path], capture_output=True, text=True)
    os.unlink(path)
    return r.stdout.strip(), r.returncode

sp = st = 0
g2 = gen_term()
for i in range(30):
    expr = next(g2)
    got, rc = snoc_run(f"OUTPUT = '{expr}'\nEND\n")
    st += 1
    if got == expr and rc == 0:
        sp += 1
    else:
        print(f"[FAIL] snoc[{i+1}]: expected {expr!r}, got {got!r}")
print(f"  {sp}/{st} passed")
passed += sp; total += st
print()

print("=================================================================")
print(f"  TOTAL: {passed}/{total} passed")
if passed == total:
    print("  The worm found nothing. Ready for Sprint 16.")
    print("  Sprint 16: compile parse_item()/parse_term() in SNOBOL4.")
else:
    print(f"  {total-passed} FAILURES. The worm works.")
print("=================================================================")
sys.exit(0 if passed == total else 1)
