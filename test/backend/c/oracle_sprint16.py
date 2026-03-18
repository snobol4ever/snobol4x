#!/usr/bin/env python3
"""
oracle_sprint16.py — Sprint 16 Oracle

The compiled expression parser (IR → emit_c.py → cc → binary) must
match every expression the worm generates.

Three checks:
  1. Every gen_term() output → binary exits 0 (MATCH)
  2. Every rand_expression() output → binary exits 0 (MATCH)
  3. Negative cases (non-expressions) → binary exits 1 (NO MATCH)

The worm. Growing token count. gen_term() is systematic. rand is random.
Together they cover the grammar exhaustively.
"""

import sys, os, subprocess, tempfile, random

HERE    = os.path.dirname(os.path.abspath(__file__))
ROOT    = os.path.join(HERE, '..', '..')
RUNTIME = os.path.abspath(os.path.join(ROOT, 'src', 'runtime'))

sys.path.insert(0, os.path.join(ROOT, 'src', 'ir'))
sys.path.insert(0, os.path.join(ROOT, 'src', 'codegen'))
sys.path.insert(0, os.path.join(HERE, '..', 'sprint15'))

from ir     import Graph, Lit, Alt, Cat, Span, Pos, Rpos, Ref
from emit_c import emit_program

# Load Expressions.py generators
_ns  = {}
_src = open(os.path.join(HERE, '..', 'sprint15', 'Expressions.py')).read()
_src = _src[:_src.index("import sys")]
exec(_src, _ns)
gen_term        = _ns['gen_term']
rand_expression = _ns['rand_expression']

# ---------------------------------------------------------------------------
# Build the expression parser binary
# ---------------------------------------------------------------------------

def build_graph():
    g = Graph()
    g.add('ITEM',
        Alt(Span('0123456789'),
        Alt(Lit('x'),
        Alt(Lit('y'),
        Alt(Lit('z'),
        Cat(Lit('('), Cat(Ref('TERM'), Lit(')'))))))))
    g.add('ELEMENT',
        Alt(Ref('ITEM'),
        Alt(Cat(Lit('+'), Ref('ELEMENT')),
            Cat(Lit('-'), Ref('ELEMENT')))))
    g.add('FACTOR',
        Alt(Ref('ELEMENT'),
        Alt(Cat(Ref('ELEMENT'), Cat(Lit('*'), Ref('FACTOR'))),
            Cat(Ref('ELEMENT'), Cat(Lit('/'), Ref('FACTOR'))))))
    g.add('TERM',
        Alt(Ref('FACTOR'),
        Alt(Cat(Ref('FACTOR'), Cat(Lit('+'), Ref('TERM'))),
            Cat(Ref('FACTOR'), Cat(Lit('-'), Ref('TERM'))))))
    g.add('EXPRESSION',
        Cat(Pos(0), Cat(Ref('TERM'), Rpos(0))))
    return g

def build_binary():
    g   = build_graph()
    src = emit_program(g, root_name='EXPRESSION', subject='', include_main=True)
    src = src.replace('#include "../../src/runtime/runtime.h"',
                      f'#include "{RUNTIME}/runtime.h"')
    src = src.replace('    Sigma = "";\n',
                      '    Sigma = (argc > 1) ? argv[1] : "";\n')
    src = src.replace('int main(void) {', 'int main(int argc, char **argv) {')

    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as f:
        f.write(src); c_path = f.name

    bin_path = c_path.replace('.c', '')
    r = subprocess.run(
        ['cc', '-O2', '-o', bin_path, c_path,
         f'{RUNTIME}/runtime.c', f'-I{RUNTIME}'],
        capture_output=True, text=True)
    os.unlink(c_path)

    if r.returncode != 0:
        print("COMPILE ERROR:", r.stderr, file=sys.stderr)
        return None
    return bin_path

def match(binary, expr):
    r = subprocess.run([binary, expr], capture_output=True)
    return r.returncode == 0

# ---------------------------------------------------------------------------
print("=== Sprint 16 Oracle — The Worm vs Compiled Parser ===")
print()
print("Building expression parser binary from IR...")
binary = build_binary()
if not binary:
    sys.exit(1)
print(f"  Binary built: {binary}")
print()

passed = total = 0

def check(desc, expr, expect_match):
    global passed, total
    total += 1
    got = match(binary, expr)
    ok  = (got == expect_match)
    if not ok:
        expected_s = "MATCH" if expect_match else "NO MATCH"
        got_s      = "MATCH" if got          else "NO MATCH"
        print(f"[FAIL] {desc}: {expr!r}")
        print(f"       expected {expected_s}, got {got_s}")
    else:
        passed += 1

# Phase 1: gen_term() systematic growing-token coverage
print("Phase 1: gen_term() systematic (N=100)")
g = gen_term()
lens = []
for i in range(100):
    expr = next(g)
    lens.append(len(expr))
    check(f"gen[{i+1:3d}] len={len(expr):4d}", expr, expect_match=True)
print(f"  lengths: 1..{max(lens)}  |  {passed}/{total} passed")
print()

# Phase 2: rand_expression() random worm
print("Phase 2: rand_expression() random worm (N=500)")
random.seed(42)
p0, t0 = passed, total
for i in range(500):
    expr = rand_expression()
    check(f"rand[{i:3d}]", expr, expect_match=True)
print(f"  {passed-p0}/{total-t0} passed")
print()

# Phase 3: negative cases — these must NOT match
print("Phase 3: negative cases (must NOT match)")
negatives = [
    '',           # empty
    '!!',         # invalid chars
    'x+',         # incomplete
    '+',          # bare operator
    '()',         # empty parens
    '(x',         # unclosed paren
    'x y',        # space (not in grammar)
    'x++',        # trailing operator
    '**x',        # leading operator
]
p0, t0 = passed, total
for expr in negatives:
    check(f"neg {expr!r}", expr, expect_match=False)
print(f"  {passed-p0}/{total-t0} passed")
print()

# Cleanup
os.unlink(binary)

# Summary
print("=================================================================")
print(f"  TOTAL: {passed}/{total} passed")
if passed == total:
    print()
    print("  The worm found nothing.")
    print("  Compiled expression parser matches Python reference on all cases.")
    print()
    print("  Sprint 17: add evaluate() — full round-trip in compiled SNOBOL4.")
else:
    print(f"  {total-passed} FAILURES.")
print("=================================================================")
sys.exit(0 if passed == total else 1)
