#!/usr/bin/env python3
"""wrap_swi.py — wrap a SWI plunit test file for snobol4x JVM backend.

Statically extracts test/N registrations and emits pj_test/4 facts,
avoiding the need for begin_tests/end_tests to execute as directives.
"""

import sys, re, os

STRIP_RE = re.compile(
    r'^:-\s*(module|use_module|ensure_loaded|style_check|'
    r'set_prolog_flag|if|else|endif|reexport|load_files|multifile)\s*[(\[]',
    re.IGNORECASE
)
STRIP_BARE_RE = re.compile(
    r'^:-\s*(dynamic|discontiguous|meta_predicate|multifile|'
    r'module_transparent|use_module|ensure_loaded)\s+\w',
    re.IGNORECASE
)

def read_shim():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'plunit.pl')) as f:
        return f.read()

def normalize_opts(opts_str):
    """Normalize test/2 options so the result is safe in a Prolog fact.

    SWI test/2 second-arg can be:
      - a list:      [sto(rational_trees), true(X==y)]   → keep as-is
      - true(Expr):  true(X==y)                          → keep as-is
      - bare goal:   X == y                              → wrap as true(X==y)
      - atom:        fail / nondet / det                 → keep as-is

    The critical issue: bare goals like `X == y` contain unbound variables
    which are illegal in Prolog facts.  Wrapping as true(X==y) keeps the
    variable scoped to the clause that uses it (run_true/4 in plunit shim).
    """
    s = opts_str.strip()
    # Already a list or already wrapped in a known functor
    if s.startswith('['):
        return s
    # Check for known atom options
    if re.match(r'^(fail|false|nondet|det|true)$', s):
        return s
    # Already a compound with known option functor
    if re.match(r'^(true|error|throws|all|setup|cleanup|condition|sto|forall)\s*\(', s):
        return s
    # Bare goal (e.g. X == y, Shared == [...]) — wrap as true(...)
    return f'true({s})'

def parse_test_clauses(lines):
    """Extract (suite, name, options) from test/1 and test/2 clause heads."""
    suite = None
    tests = []
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        # begin_tests(Suite)
        m = re.match(r'^:-\s*begin_tests\((\w+)', s)
        if m:
            suite = m.group(1)
            i += 1; continue
        m = re.match(r'^:-\s*end_tests\(', s)
        if m:
            suite = None
            i += 1; continue
        if suite is None:
            i += 1; continue
        # test(Name) :- or test(Name, Opts) :-
        m = re.match(r'^test\((\w+)\)\s*:-', s)
        if m:
            tests.append((suite, m.group(1), 'none'))
            i += 1; continue
        m = re.match(r'^test\((\w+),\s*(.+)\)\s*:-', s)
        if m:
            opts = normalize_opts(m.group(2).strip())
            tests.append((suite, m.group(1), opts))
            i += 1; continue
        i += 1
    return tests

def collect_suites(lines):
    suites = []
    for l in lines:
        m = re.match(r'^\s*:-\s*begin_tests\((\w+)', l.strip())
        if m and m.group(1) not in suites:
            suites.append(m.group(1))
    return suites

def wrap(inpath, out):
    with open(inpath) as f:
        lines = f.readlines()

    suites  = collect_suites(lines)
    tests   = parse_test_clauses(lines)

    in_block_comment = False
    for line in lines:
        s = line.strip()
        if '/*' in line and '*/' not in line:
            in_block_comment = True
        if '*/' in line:
            in_block_comment = False
            out.write(line); continue
        if in_block_comment:
            out.write(line); continue
        # strip directives
        if STRIP_RE.match(s) or STRIP_BARE_RE.match(s):
            out.write(f'% [stripped] {line.rstrip()}\n'); continue
        # silence begin/end_tests directives (keep as comments)
        if re.match(r'^:-\s*(begin_tests|end_tests)\b', s):
            out.write(f'% [kept-passive] {line.rstrip()}\n'); continue
        out.write(line)

    # emit suite facts
    out.write('\n% ===== suite registrations (static) =====\n')
    for suite in suites:
        out.write(f'pj_suite({suite}).\n')

    # emit test facts: pj_test(Suite, Name, Options, Goal)
    out.write('\n% ===== test registrations (static) =====\n')
    for (suite, name, opts) in tests:
        if opts == 'none':
            out.write(f'pj_test({suite}, {name}, [], ({suite}_{name})).\n')
        else:
            out.write(f'pj_test({suite}, {name}, {opts}, ({suite}_{name})).\n')

    # emit bridge predicates: suite_name :- test body lookup
    out.write('\n% ===== test body bridges =====\n')
    for (suite, name, _opts) in tests:
        out.write(f'{suite}_{name} :- test({name}).\n')

    # plunit shim
    out.write('\n% ===== plunit shim =====\n')
    out.write(read_shim())

    # entry point
    out.write('\n% ===== entry point =====\n')
    out.write(':- initialization(main).\n')
    out.write('main :- run_tests, halt.\n')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: wrap_swi.py input.pl [output.pro]', file=sys.stderr)
        sys.exit(1)
    inpath = sys.argv[1]
    if len(sys.argv) >= 3:
        with open(sys.argv[2], 'w') as f:
            wrap(inpath, f)
    else:
        wrap(inpath, sys.stdout)
