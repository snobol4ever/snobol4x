#!/usr/bin/env python3
"""
build_expr_parser.py — Sprint 16

Translates Expressions.py parse_* functions mechanically to IR nodes,
emits C via emit_c.py, compiles with cc, produces a standalone binary
that accepts an expression on argv[1] and exits 0 (match) or 1 (no match).

The translation is exact and mechanical:
  Python generator function  →  named IR node
  for _1 in σ("x"):         →  Lit("x")
  for _1 in SPAN("0-9"):    →  Span("0123456789")
  sequential for loops       →  Alt(left, right)
  nested for loops           →  Cat(outer, inner)
  parse_term() reference     →  Ref("TERM")
  POS(0) / RPOS(0)           →  Pos(0) / Rpos(0)
"""

import sys, os, subprocess, tempfile

HERE    = os.path.dirname(os.path.abspath(__file__))
ROOT    = os.path.join(HERE, '..', '..')
RUNTIME = os.path.join(ROOT, 'src', 'runtime')

sys.path.insert(0, os.path.join(ROOT, 'src', 'ir'))
sys.path.insert(0, os.path.join(ROOT, 'src', 'codegen'))

from ir    import Graph, Lit, Alt, Cat, Span, Pos, Rpos, Ref
from emit_c import emit_program


def build_graph():
    g = Graph()

    # ITEM = SPAN('0123456789') | 'x' | 'y' | 'z' | '(' TERM ')'
    g.add('ITEM',
        Alt(Span('0123456789'),
        Alt(Lit('x'),
        Alt(Lit('y'),
        Alt(Lit('z'),
        Cat(Lit('('), Cat(Ref('TERM'), Lit(')'))))))))

    # ELEMENT = ITEM | '+' ELEMENT | '-' ELEMENT
    g.add('ELEMENT',
        Alt(Ref('ITEM'),
        Alt(Cat(Lit('+'), Ref('ELEMENT')),
            Cat(Lit('-'), Ref('ELEMENT')))))

    # FACTOR = ELEMENT | ELEMENT '*' FACTOR | ELEMENT '/' FACTOR
    g.add('FACTOR',
        Alt(Ref('ELEMENT'),
        Alt(Cat(Ref('ELEMENT'), Cat(Lit('*'), Ref('FACTOR'))),
            Cat(Ref('ELEMENT'), Cat(Lit('/'), Ref('FACTOR'))))))

    # TERM = FACTOR | FACTOR '+' TERM | FACTOR '-' TERM
    g.add('TERM',
        Alt(Ref('FACTOR'),
        Alt(Cat(Ref('FACTOR'), Cat(Lit('+'), Ref('TERM'))),
            Cat(Ref('FACTOR'), Cat(Lit('-'), Ref('TERM'))))))

    # EXPRESSION = POS(0) TERM RPOS(0)
    g.add('EXPRESSION',
        Cat(Pos(0), Cat(Ref('TERM'), Rpos(0))))

    return g


def build_binary(subject='', out_path=None):
    """
    Build the expression parser binary.
    subject: default subject string baked in (can be overridden at runtime).
    out_path: where to write the binary (temp file if None).
    Returns path to binary.
    """
    g   = build_graph()
    src = emit_program(g, root_name='EXPRESSION',
                       subject=subject, include_main=True)

    # Fix runtime include
    src = src.replace(
        '#include "../../src/runtime/runtime.h"',
        f'#include "{RUNTIME}/runtime.h"'
    )

    # Patch main() to accept subject from argv[1]
    src = src.replace(
        f'const char *subject    = "{subject}";',
        '    const char *subject    = (argc > 1) ? argv[1] : "";',
    )
    src = src.replace(
        'int main(void) {',
        'int main(int argc, char **argv) {'
    )

    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as f:
        f.write(src)
        c_path = f.name

    if out_path is None:
        out_path = c_path.replace('.c', '')

    result = subprocess.run(
        ['cc', '-O2', '-o', out_path, c_path,
         os.path.join(RUNTIME, 'runtime.c'), f'-I{RUNTIME}'],
        capture_output=True, text=True
    )
    os.unlink(c_path)

    if result.returncode != 0:
        print("Compile error:", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return None

    return out_path


if __name__ == '__main__':
    import tempfile
    print("Building expression parser binary...")
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as f:
        bin_path = f.name

    path = build_binary(out_path=bin_path)
    if path:
        print(f"Binary: {path}")
        # Quick smoke test
        for expr in ['x', 'x+y', '42', '(x+y)*z', 'INVALID!!']:
            r = subprocess.run([path, expr], capture_output=True)
            status = 'MATCH' if r.returncode == 0 else 'NO MATCH'
            print(f"  {expr!r:20s} → {status}")
        os.unlink(path)
