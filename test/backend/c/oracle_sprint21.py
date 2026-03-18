"""
oracle_sprint21.py — Sprint 21: Any, Break, Notany primitives in FlatEmitter

Verifies that emit_c.py correctly emits Any, Break, and Notany nodes
and that the generated C compiles and produces correct output.
"""
import sys, os, subprocess, tempfile

ROOT     = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CODEGEN  = os.path.join(ROOT, 'src', 'codegen')
IR_DIR   = os.path.join(ROOT, 'src', 'ir')
sys.path.insert(0, CODEGEN)
sys.path.insert(0, IR_DIR)

from ir import Graph, Cat, Pos, Rpos, Lit, Any, Break, Notany, Assign, Alt
from emit_c import emit_program

passed = 0; failed = 0

RUNTIME_STUB = r"""
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { const char *ptr; int64_t len; } str_t;
static void sno_output(str_t s) { printf("%.*s\n", (int)s.len, s.ptr); }
static void sno_output_cstr(const char *s) { printf("%s\n", s); }
static void sno_arena_reset(void) {}
static void* sno_enter(void **zz, int sz) { if (!*zz) *zz = calloc(1,sz); return *zz; }
static void sno_exit(void **zz) { *zz = 0; }
"""

def compile_run(label, graph, root, subject, expected_out, expected_rc):
    global passed, failed
    src = emit_program(graph, root, subject=subject, include_main=True)
    src = src.replace('#include "../../src/runtime/runtime.h"', RUNTIME_STUB)
    with tempfile.NamedTemporaryFile(suffix='.c', mode='w', delete=False) as f:
        f.write(src); fname = f.name
    exe = fname.replace('.c', '')
    r = subprocess.run(['cc', '-o', exe, fname], capture_output=True)
    if r.returncode != 0:
        print(f"  FAIL(compile) {label}: {r.stderr.decode()[:300]}")
        failed += 1; return
    r2 = subprocess.run([exe], capture_output=True, text=True)
    os.unlink(fname); os.unlink(exe)
    got_out = r2.stdout.strip()
    got_rc  = r2.returncode
    if got_out == expected_out and got_rc == expected_rc:
        print(f"  PASS: {label} → {got_out!r}")
        passed += 1
    else:
        print(f"  FAIL: {label}")
        print(f"    expected out={expected_out!r} rc={expected_rc}")
        print(f"    got     out={got_out!r}     rc={got_rc}")
        failed += 1

print("\n[1. ANY — match one char from charset]")
# POS(0) ANY("aeiou") . OUTPUT  on "apple" → captures "a", succeeds
g = Graph()
g.add("root", Cat(Pos(0), Assign(Any("aeiou"), "OUTPUT")))
compile_run("ANY match 'apple'", g, "root", "apple", "a\nSuccess!", 0)

# ANY("aeiou") at cursor=0 on "xyz" → fails (x not a vowel)
g = Graph()
g.add("root", Cat(Pos(0), Any("aeiou")))
compile_run("ANY fail 'xyz'",   g, "root", "xyz", "Failure.", 1)

print("\n[2. BREAK — scan to delimiter, do not consume it]")
# POS(0) BREAK(" ") . OUTPUT  on "hello world" → captures "hello"
g = Graph()
g.add("root", Cat(Pos(0), Assign(Break(" "), "OUTPUT")))
compile_run("BREAK(' ') 'hello world'", g, "root", "hello world", "hello\nSuccess!", 0)

# POS(0) BREAK(";") . OUTPUT  on "foo;bar" → captures "foo"
g = Graph()
g.add("root", Cat(Pos(0), Assign(Break(";"), "OUTPUT")))
compile_run("BREAK(';') 'foo;bar'", g, "root", "foo;bar", "foo\nSuccess!", 0)

# BREAK on string with no delimiter → fails
g = Graph()
g.add("root", Cat(Pos(0), Break(";")))
compile_run("BREAK fail no delimiter", g, "root", "nocolon", "Failure.", 1)

print("\n[3. NOTANY — match one char NOT in charset]")
# POS(0) NOTANY("aeiou") . OUTPUT  on "xyz" → captures "x"
g = Graph()
g.add("root", Cat(Pos(0), Assign(Notany("aeiou"), "OUTPUT")))
compile_run("NOTANY match 'xyz'",  g, "root", "xyz",   "x\nSuccess!", 0)

# NOTANY("aeiou") on "apple" → fails (a IS a vowel)
g = Graph()
g.add("root", Cat(Pos(0), Notany("aeiou")))
compile_run("NOTANY fail 'apple'", g, "root", "apple", "Failure.", 1)

print("\n[4. BREAK then LIT — classic SNOBOL4 token scan]")
# BREAK(" ") . OUTPUT LIT(" ")  on "foo bar" → captures "foo"
g = Graph()
g.add("root", Cat(Pos(0), Cat(Assign(Break(" "), "OUTPUT"), Lit(" "))))
compile_run("BREAK+LIT 'foo bar'", g, "root", "foo bar", "foo\nSuccess!", 0)

print("\n[5. ALT with Any]")
# ALT(LIT("Bird"), ANY("ABCDE"))  on "Bird" → "Bird"
g = Graph()
g.add("root", Cat(Pos(0), Assign(Alt(Lit("Bird"), Any("ABCDE")), "OUTPUT")))
compile_run("ALT Lit|Any 'Bird'", g, "root", "Bird", "Bird\nSuccess!", 0)

# Same pattern on "Ace" → "A" (Any matches)
g = Graph()
g.add("root", Cat(Pos(0), Assign(Alt(Lit("Bird"), Any("ABCDE")), "OUTPUT")))
compile_run("ALT Lit|Any 'Ace'",  g, "root", "Ace",  "A\nSuccess!",    0)

print(f"\n============================================================")
print(f"Results: {passed} passed, {failed} failed")
if failed == 0:
    print("ALL PASS — Sprint 21 Any/Break/Notany oracle green.")
sys.exit(0 if failed == 0 else 1)
