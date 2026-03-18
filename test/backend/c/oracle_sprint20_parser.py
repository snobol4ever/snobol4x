"""
oracle_sprint20_parser.py — Sprint 20 parser oracle

Tests the full SNOBOL4 statement parser (sno_parser.py) against:
  1. Unit tests for each statement structure
  2. Integration: beauty.sno parses 1104/316/0 (stmts/labels/failures)
"""

import sys
import os

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'src', 'parser'))
sys.path.insert(0, os.path.join(ROOT, 'src', 'ir'))

from sno_parser import parse_source, parse_file
from ir import Expr, PatExpr, Goto, Stmt, Program

BEAUTY_RUN = os.path.abspath(os.path.join(ROOT, '..', 'snobol4corpus',
                              'programs', 'beauty', 'beauty.sno'))

passed = 0
failed = 0

def check(desc, cond):
    global passed, failed
    if cond:
        print(f'  PASS: {desc}')
        passed += 1
    else:
        print(f'  FAIL: {desc}')
        failed += 1

def parse(src):
    return parse_source(src)

# ---------------------------------------------------------------------------
print('\n[1. Basic assignment]')
prog = parse("    X = 'hello'\n")
check('one stmt', len(prog.stmts) == 1)
s = prog.stmts[0]
check('no label', s.label is None)
check('subject var X', s.subject and s.subject.kind == 'var' and s.subject.val == 'X')
check('replacement str hello', s.replacement and s.replacement.kind == 'str' and s.replacement.val == 'hello')

# ---------------------------------------------------------------------------
print('\n[2. Label detection]')
prog = parse("LOOP  X = X + 1  :(LOOP)\n")
check('label LOOP', prog.stmts[0].label == 'LOOP')
check('unconditional goto LOOP', prog.stmts[0].goto.unconditional == 'LOOP')

prog = parse("END\n")
check('bare label END', prog.stmts[0].label == 'END')

# ---------------------------------------------------------------------------
print('\n[3. Goto variants]')
prog = parse("    X = 1  :S(YES)F(NO)\n")
s = prog.stmts[0]
check(':S(YES)', s.goto.on_success == 'YES')
check(':F(NO)',  s.goto.on_failure == 'NO')

prog = parse("    X = 1  :F(FAIL)\n")
check(':F only', prog.stmts[0].goto.on_failure == 'FAIL')

prog = parse("    X = 1  :(DONE)\n")
check(':(DONE)', prog.stmts[0].goto.unconditional == 'DONE')

# ---------------------------------------------------------------------------
print('\n[4. Arithmetic]')
prog = parse("    X = A + B\n")
check('add', prog.stmts[0].replacement.kind == 'add')

prog = parse("    X = A * B + C\n")
check('add(mul,C) precedence', prog.stmts[0].replacement.kind == 'add')

prog = parse("    X = A ** 2\n")
check('pow', prog.stmts[0].replacement.kind == 'pow')

prog = parse("    X = -A\n")
check('unary neg', prog.stmts[0].replacement.kind == 'neg')

prog = parse("    X = A - B\n")
check('sub', prog.stmts[0].replacement.kind == 'sub')

prog = parse("    X = A / B\n")
check('div', prog.stmts[0].replacement.kind == 'div')

# ---------------------------------------------------------------------------
print('\n[5. Function calls]')
prog = parse("    X = SIZE(Y)\n")
r = prog.stmts[0].replacement
check('SIZE call', r.kind == 'call' and r.name == 'SIZE')
check('SIZE 1 arg', len(r.args) == 1)

prog = parse("    DEFINE('foo(a,b)c')\n")
s = prog.stmts[0].subject
check('DEFINE call', s.kind == 'call' and s.name == 'DEFINE')

prog = parse("    DATA('tree(t,v,n,c)')  :(TreeEnd)\n")
check('DATA call + goto', prog.stmts[0].subject.name == 'DATA' and
      prog.stmts[0].goto.unconditional == 'TreeEnd')

prog = parse("    X = GT(xTrace, 4) 'msg'\n")
check('GT call replacement', prog.stmts[0].replacement.kind in ('call', 'concat'))

# ---------------------------------------------------------------------------
print('\n[6. Array subscript]')
prog = parse("    ppStop[1] = 18\n")
s = prog.stmts[0].subject
check('array subscript', s.kind == 'array')
check('subscript index 1', s.subscripts[0].val == 1)

prog = parse("    ppStop[1] = ppStop[2]\n")
check('array on both sides', prog.stmts[0].replacement.kind == 'array')

# ---------------------------------------------------------------------------
print('\n[7. Indirect variable $expr]')
prog = parse("    $'#N' = 0\n")
s = prog.stmts[0].subject
check("indirect $'#N' subject", s.kind == 'indirect')
check("indirect child is str '#N'", s.child.kind == 'str' and s.child.val == '#N')

# ---------------------------------------------------------------------------
print('\n[8. Pattern expressions]')
prog = parse("    X  SPAN('0123456789')\n")
check('SPAN pattern', prog.stmts[0].pattern.kind == 'call' and prog.stmts[0].pattern.name == 'SPAN')

prog = parse("    X  *snoPat\n")
check('deferred ref *snoPat', prog.stmts[0].pattern.kind == 'ref' and prog.stmts[0].pattern.name == 'snoPat')

prog = parse("    X  'a' | 'b'\n")
check('alt pattern', prog.stmts[0].pattern.kind == 'alt')

prog = parse("    X  SPAN(digits) $ tok\n")
check('immediate capture', prog.stmts[0].pattern.kind == 'assign_imm')

prog = parse("    X  BREAK(nl) . result\n")
check('conditional capture', prog.stmts[0].pattern.kind == 'assign_cond')

# ---------------------------------------------------------------------------
print('\n[9. Keywords]')
prog = parse("    &FULLSCAN = 1\n")
s = prog.stmts[0].subject
check('keyword &FULLSCAN', s.kind == 'keyword' and s.val == 'FULLSCAN')

# ---------------------------------------------------------------------------
print('\n[10. Continuation lines]')
src = "    snoReal = SPAN(digits)\n+              'more'\n"
prog = parse(src)
check('continuation = 1 stmt', len(prog.stmts) == 1)

# ---------------------------------------------------------------------------
print('\n[11. Multiple statements]')
src = "    A = 1\n    B = 2\n    C = A + B\n"
prog = parse(src)
check('3 stmts', len(prog.stmts) == 3)
check('third = add', prog.stmts[2].replacement.kind == 'add')

# ---------------------------------------------------------------------------
print('\n[12. Comments skipped]')
src = "* This is a comment\n    X = 1\n* Another comment\n    Y = 2\n"
prog = parse(src)
check('comments skipped, 2 stmts', len(prog.stmts) == 2)

# ---------------------------------------------------------------------------
print('\n[13. Integration: beauty.sno (full -INCLUDE expansion)]')
if os.path.exists(BEAUTY_RUN):
    INC_DIR = os.path.abspath(os.path.join(ROOT, '..', 'snobol4corpus', 'programs', 'inc'))
    prog    = parse_file(BEAUTY_RUN, include_dirs=[INC_DIR])
    labels  = [s.label for s in prog.stmts if s.label]
    empties = [s for s in prog.stmts
               if s.subject is None and s.pattern is None
               and s.replacement is None and s.goto is None
               and s.label is None]

    check('1214 statements total',   len(prog.stmts) == 1214)
    check('311 labels',              len(labels) == 311)
    check('0 empty (parse failures)',len(empties) == 0)

    label_set = set(labels)
    for lbl in ['START', 'G1', 'InitCounter', 'PushCounter', 'PopCounter',
                'IncCounter', 'DecCounter', 'TopCounter', 'CounterEnd',
                'InitBegTag', 'TreeEnd', 'lwr', 'upr', 'lwr_end', 'upr_end']:
        check(f'  label {lbl}', lbl in label_set)
else:
    print(f'  SKIP: {BEAUTY_RUN} not found')

# ---------------------------------------------------------------------------
print(f'\n============================================================')
print(f'Results: {passed} passed, {failed} failed')
if failed == 0:
    print('ALL PASS — Sprint 20 parser oracle green.')
sys.exit(0 if failed == 0 else 1)
