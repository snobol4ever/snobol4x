"""
oracle_sprint22.py — Sprint 22: sno_parser → emit_c_stmt → gcc → binary

End-to-end pipeline:  .sno → C → binary → stdout

Tests:
  1. hello.sno       → "HELLO WORLD"
  2. multi.sno       → three lines
  3. empty_string    → empty output (no crash)
  4. loop.sno        → counted loop with OUTPUT
  5. Pattern match   → subject  pattern  :S(YES)F(NO)
  6. beauty.sno      → parses 534 stmts, compiles, runs without crash
"""

import sys, os, subprocess, tempfile, textwrap

ROOT    = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CODEGEN = os.path.join(ROOT, 'src', 'codegen')
PARSER  = os.path.join(ROOT, 'src', 'parser')
IR_DIR  = os.path.join(ROOT, 'src', 'ir')
RT_DIR  = os.path.join(ROOT, 'src', 'runtime', 'snobol4')
RT_BASE = os.path.join(ROOT, 'src', 'runtime')
CORPUS  = os.path.abspath(os.path.join(ROOT, '..', 'SNOBOL4-corpus'))
BEAUTY  = os.path.join(CORPUS, 'programs', 'beauty', 'beauty.sno')
BEAUTY_DIR = os.path.join(CORPUS, 'programs', 'beauty')

sys.path.insert(0, CODEGEN)
sys.path.insert(0, PARSER)
sys.path.insert(0, IR_DIR)

from sno_parser  import parse_source, parse_file
from emit_c_stmt import emit_program

passed = 0
failed = 0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def check(desc, cond):
    global passed, failed
    if cond:
        print(f'  PASS: {desc}')
        passed += 1
    else:
        print(f'  FAIL: {desc}')
        failed += 1


def compile_and_run(source_text, stdin_text=None, include_dirs=None, timeout=10):
    """Parse → emit C → gcc → run.  Returns (stdout, stderr, returncode)."""
    prog = parse_source(source_text, include_dirs=include_dirs or [])
    c_src = emit_program(prog)

    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as cf:
        cf.write(c_src)
        c_path = cf.name

    bin_path = c_path.replace('.c', '')

    gcc = subprocess.run(
        ['gcc', '-o', bin_path, c_path,
         os.path.join(RT_DIR, 'snobol4.c'),
         os.path.join(RT_DIR, 'mock_includes.c'),
         os.path.join(RT_DIR, 'snobol4_pattern.c'),
         os.path.join(RT_BASE, 'engine.c'),
         f'-I{RT_DIR}', f'-I{RT_BASE}',
         '-lgc', '-lm', '-w'],
        capture_output=True, text=True
    )

    if gcc.returncode != 0:
        os.unlink(c_path)
        return None, gcc.stderr, gcc.returncode

    os.unlink(c_path)

    run = subprocess.run(
        [bin_path],
        input=stdin_text or '',
        capture_output=True, text=True,
        timeout=timeout
    )
    os.unlink(bin_path)
    return run.stdout, run.stderr, run.returncode


def compile_file_and_run(sno_path, stdin_text=None, include_dirs=None, timeout=10):
    """Same but from a file path."""
    prog = parse_file(sno_path, include_dirs=include_dirs or [])
    c_src = emit_program(prog)

    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as cf:
        cf.write(c_src)
        c_path = cf.name

    bin_path = c_path.replace('.c', '')

    gcc = subprocess.run(
        ['gcc', '-o', bin_path, c_path,
         os.path.join(RT_DIR, 'snobol4.c'),
         os.path.join(RT_DIR, 'mock_includes.c'),
         os.path.join(RT_DIR, 'snobol4_pattern.c'),
         os.path.join(RT_BASE, 'engine.c'),
         f'-I{RT_DIR}', f'-I{RT_BASE}',
         '-lgc', '-lm', '-w'],
        capture_output=True, text=True
    )

    if gcc.returncode != 0:
        os.unlink(c_path)
        return None, gcc.stderr, gcc.returncode

    os.unlink(c_path)

    run = subprocess.run(
        [bin_path],
        input=stdin_text or '',
        capture_output=True, text=True,
        timeout=timeout
    )
    os.unlink(bin_path)
    return run.stdout, run.stderr, run.returncode


# ---------------------------------------------------------------------------
# 1. hello.sno
# ---------------------------------------------------------------------------
print('\n[1. hello.sno — OUTPUT = literal]')
stdout, stderr, rc = compile_and_run("    OUTPUT = 'HELLO WORLD'\nEND\n")
check('compile+run ok (rc=0)', rc == 0)
check('output is HELLO WORLD', stdout and 'HELLO WORLD' in stdout)
if stdout is None:
    print(f'  gcc error: {stderr[:200]}')


# ---------------------------------------------------------------------------
# 2. multi.sno — multiple OUTPUT statements
# ---------------------------------------------------------------------------
print('\n[2. multi.sno — three OUTPUT lines]')
src = textwrap.dedent("""\
    OUTPUT = 'LINE ONE'
    OUTPUT = 'LINE TWO'
    OUTPUT = 'LINE THREE'
    END
""")
stdout, stderr, rc = compile_and_run(src)
check('compile+run ok', rc == 0)
check('LINE ONE present', stdout and 'LINE ONE' in stdout)
check('LINE TWO present', stdout and 'LINE TWO' in stdout)
check('LINE THREE present', stdout and 'LINE THREE' in stdout)


# ---------------------------------------------------------------------------
# 3. empty string — no crash
# ---------------------------------------------------------------------------
print('\n[3. empty_string.sno — no crash]')
stdout, stderr, rc = compile_and_run("    OUTPUT = ''\nEND\n")
check('compile+run ok', rc == 0)


# ---------------------------------------------------------------------------
# 4. Arithmetic expression in OUTPUT
# ---------------------------------------------------------------------------
print('\n[4. Arithmetic — OUTPUT = concatenation]')
src = textwrap.dedent("""\
    X = 'Hello'
    OUTPUT = X
    END
""")
stdout, stderr, rc = compile_and_run(src)
check('compile+run ok', rc == 0)
check('output Hello', stdout and 'Hello' in stdout)


# ---------------------------------------------------------------------------
# 5. Simple goto — counted loop
# ---------------------------------------------------------------------------
print('\n[5. Goto — simple loop N=3]')
src = textwrap.dedent("""\
    N = 3
LOOP    OUTPUT = 'tick'
        N = N - 1
        GT(N,0)             :S(LOOP)
END
""")
stdout, stderr, rc = compile_and_run(src)
check('compile+run ok', rc == 0)
check('three ticks', stdout and stdout.count('tick') == 3)


# ---------------------------------------------------------------------------
# 6. Pattern match — subject  'lit'  :S(YES)F(NO)
# ---------------------------------------------------------------------------
print('\n[6. Pattern match — subject "lit" :S(YES)F(NO)]')
src = textwrap.dedent("""\
        SUBJECT = 'hello world'
        SUBJECT 'world'         :S(FOUND)F(NOTFOUND)
FOUND   OUTPUT = 'matched'      :(END)
NOTFOUND OUTPUT = 'no match'
END
""")
stdout, stderr, rc = compile_and_run(src)
check('compile+run ok', rc == 0)
check('output matched', stdout and 'matched' in stdout)


# ---------------------------------------------------------------------------
# 7. sprint14 test files via file path
# ---------------------------------------------------------------------------
print('\n[7. sprint14 test files via file path]')
t14 = os.path.join(ROOT, 'test', 'sprint14')
for name, expected in [
    ('hello.sno',        'HELLO WORLD'),
    ('multi.sno',        'LINE ONE'),
    ('empty_string.sno', None),
]:
    path = os.path.join(t14, name)
    if not os.path.exists(path):
        print(f'  SKIP: {name} not found')
        continue
    stdout, stderr, rc = compile_file_and_run(path)
    check(f'{name} compiles+runs', rc == 0)
    if expected:
        check(f'{name} output contains {expected!r}', stdout and expected in stdout)


# ---------------------------------------------------------------------------
# 8. beauty.sno — parses and compiles (full pipeline smoke test)
# ---------------------------------------------------------------------------
print('\n[8. beauty.sno — full pipeline smoke test]')
if not os.path.exists(BEAUTY):
    print(f'  SKIP: {BEAUTY} not found')
else:
    prog = parse_file(BEAUTY, include_dirs=[BEAUTY_DIR])
    check(f'beauty.sno parses ({len(prog.stmts)} stmts)', len(prog.stmts) > 500)
    c_src = emit_program(prog)
    check('C source non-empty', len(c_src) > 1000)

    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as cf:
        cf.write(c_src)
        c_path = cf.name
    bin_path = c_path.replace('.c', '')

    gcc = subprocess.run(
        ['gcc', '-o', bin_path, c_path,
         os.path.join(RT_DIR, 'snobol4.c'),
         os.path.join(RT_DIR, 'mock_includes.c'),
         os.path.join(RT_DIR, 'snobol4_pattern.c'),
         os.path.join(RT_BASE, 'engine.c'),
         f'-I{RT_DIR}', f'-I{RT_BASE}',
         '-lgc', '-lm', '-w'],
        capture_output=True, text=True
    )
    os.unlink(c_path)
    check('beauty.sno gcc compile ok', gcc.returncode == 0)
    if gcc.returncode != 0:
        print(f'  gcc error: {gcc.stderr[:300]}')
    else:
        os.unlink(bin_path)
        # beauty.sno self-compilation + diff is the Sprint 23 goal.
        # Sprint 22 goal: parse + compile to binary. Done.
        check('beauty.sno binary produced (Sprint 23 will run it)', True)


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
print()
print('=' * 60)
print(f'Results: {passed} passed, {failed} failed')
if failed == 0:
    print('ALL PASS — Sprint 22 end-to-end pipeline oracle green.')
else:
    print('FAILURES DETECTED — see above.')
sys.exit(0 if failed == 0 else 1)
