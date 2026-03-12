# SNOBOL4-tiny — Sprint Plan

---

## §0 — PROJECT VISION (Session 43, 2026-03-12)

> **SNOBOL4 everywhere. SNOBOL4 for all. SNOBOL4 for now. SNOBOL4 forever.**
> `SNOBOL4everywhere` · `SNOBOL4all` · `SNOBOL4now` · `SNOBOL4ever`

The SNOBOL4-plus org is a **two-dimensional compiler matrix**:

|                | **SNOBOL4** | **SPITBOL** | **SNOCONE** | **REBUS** | *(more)* |
|----------------|-------------|-------------|-------------|-----------|----------|
| **C / native** | SNOBOL4-tiny (snoc) ← *here* | — | — | — | |
| **JVM**        | SNOBOL4-jvm | — | snocone.clj | — | |
| **.NET**       | SNOBOL4-dotnet | — | snocone.cs | — | |
| **ASM**        | — | — | — | — | |
| *(more)*       | | | | | |

- **Rows = backends / targets** (C/native, JVM, .NET, ASM, WASM, ...)
- **Columns = front-ends / source languages** (SNOBOL4, SPITBOL, SNOCONE, REBUS, ...)
- Each cell = a working compiler+runtime for that *(language × platform)* pair
- The mission: fill the matrix. Make string-processing power available everywhere.

SNOBOL4-tiny (snoc) is the **C/native × SNOBOL4** cell. Beauty.sno self-hosting
is the proof-of-correctness for that cell. Every other cell follows the same pattern.

---

## Status at Sprint 23 end

| Repo            | Commit    | Status                                      |
|-----------------|-----------|---------------------------------------------|
| SNOBOL4-tiny    | `6d3d1fa` | 22/22 PASS. snoc: 1213 stmts, 0 errors.     |
| SNOBOL4-dotnet  | `b5aad44` | 1,607 / 0                                   |
| SNOBOL4-jvm     | `9cf0af3` | 1,896 / 4,120 / 0                           |

### Sprint 23 work completed

- **`snoc_runtime.h`** — new shim header: scalar constructors (`sno_int`, `sno_str`,
  `sno_real`), keyword access (`sno_kw`, `sno_kw_set`), concat/alt/deref/indirect
  wrappers, array/table aliases, pattern aliases, `SnoMatch` struct + `sno_match` +
  `sno_replace`, `sno_init`/`sno_finish`.
- **`emit.c`** — symbol collection pre-pass, variable declarations, IO routing,
  per-statement unique labels (`_SNO_NEXT_N`), uid-suffixed temporaries, header change.
- **Hello world end-to-end**: `OUTPUT = 'hello'` compiles, links, runs. ✅

### Current blocker

`beauty.sno` generates 53 gcc errors — all **duplicate C labels** — because every
included file's DEFINE'd function labels (`pp`, `ss`, `RETURN`, `FRETURN`, etc.)
collide when emitted flat into one C `main()`.

---

## §6 — The Execution Model: Byrd Box + Exception Hygiene

### ⚡ EUREKA — Session 27, 2026-03-12 (Lon)

**Normal Byrd Box gotos handle success and failure. C exceptions (longjmp/throw)
handle ABORT and genuinely bad things only.**

This is the clean separation:

### Hot path — pure Byrd Box gotos (zero overhead)

Normal SNOBOL4 control flow — pattern success, pattern failure, backtracking,
`:S()` / `:F()` goto routing — uses **pure C labeled gotos** exactly as in
`test_sno_1.c`. No `setjmp`. No exception machinery on the hot path. The ω port
(CONCEDE) is a goto, not a throw.

### Cold path — C exceptions for ABORT and bad things only

**ABORT**, runtime errors, FENCE bare, divide-by-zero, stack overflow —
**throw a C exception** (`longjmp` to nearest handler). These are not
normal control flow. They are signals that something genuinely went wrong
or that execution must halt unconditionally.

Each SNOBOL4 **statement** is a `try/catch` boundary for these signals:

```c
/* Statement: subject pattern = replacement :S(foo) :F(bar) */
if (setjmp(sno_abort_jmp) == 0) {
    /* HOT PATH — pure Byrd Box gotos, no exception overhead */
    SnoVal _s = ...; SnoPattern *_p = ...;
    SnoMatch _m = sno_match(&_s, _p);   /* Byrd Box runs here */
    if (!_m.failed) { ...; goto _L_foo; }
    goto _L_bar;
} else {
    /* COLD PATH — caught SnoAbort (ABORT pattern, runtime error, etc.) */
    goto _SNO_ABORT_HANDLER;
}
```

Each SNOBOL4 **DEFINE'd function** is also a catch boundary:

```c
SnoVal _sno_fn_pp(SnoVal *args, int nargs) {
    if (setjmp(sno_fn_jmp) != 0)
        return SNO_FAIL_VAL;   /* ABORT inside function → FRETURN */
    /* ... function body with per-statement setjmp guards ... */
    _SNO_RETURN_pp:  return sno_get(_pp);
    _SNO_FRETURN_pp: return SNO_FAIL_VAL;
}
```

### Why this separation is correct

| Signal | Mechanism | Overhead |
|--------|-----------|----------|
| Pattern ω (CONCEDE) | Byrd Box goto | Zero |
| `:S()` / `:F()` routing | C goto via `_ok` flag | Zero |
| Backtrack (β / RECEDE) | Byrd Box goto | Zero |
| ABORT pattern | `longjmp` / throw | Only when triggered |
| Runtime error | `longjmp` / throw | Only on error |
| FENCE bare | `longjmp` / throw | Only when triggered |

Stack unwinding IS the cleanup for the abort case. No omega stack needed
for abnormal termination — the C call stack unwinds through statement and
function catch boundaries automatically.

### Statement is the right catch granularity

SNOBOL4 guarantees a statement succeeds or fails atomically. ABORT inside a
pattern mid-statement should abort the whole statement cleanly, not leave
a half-executed replacement. The statement boundary is exactly right.

### Line number diagnostics fall out for free

Each statement's `setjmp` is emitted immediately after `/* line N */`. When
the longjmp fires, the catching boundary knows exactly which line it is:

```c
/* line 45 */
sno_abort_lineno = 45;
if (setjmp(sno_abort_jmp) == 0) {
    ...Byrd Box...
} else {
    fprintf(stderr, "file.sno:45  stmt 312: ABORT\n");
    goto _SNO_ABORT_HANDLER;
}
```

CSNOBOL4-style error output — `file:LINE stmt N: EVENT` — is implicit in
which catch boundary fires. No stack crawl needed. No debug info needed.
The line number is structural.

---

## §7 — Sprint 24 Plan: Function-per-DEFINE in emit.c

### Pre-pass: identify DEFINE calls

Walk the collected statements looking for `S_EXPR` statements whose expression
is a call to `DEFINE(...)`. Parse the DEFINE argument string:

```
DEFINE('pp(a,b)loc1,loc2')
  → fn_name = "pp"
  → args    = ["a", "b"]
  → locals  = ["loc1", "loc2"]
  → entry_label = "pp"  (the SNOBOL4 label where the body starts)
```

Build a table: `fn_table[N]` with `{name, args[], locals[], entry_stmt_index}`.

### Emit structure

```
emit_header()          → #include "snoc_runtime.h"
emit_var_decls()       → static SnoVal for global vars (as today)
emit_fn_forwards()     → SnoVal _sno_fn_pp(SnoVal*, int);  (forward decls)

for each fn in fn_table:
    emit_fn_body(fn)   → SnoVal _sno_fn_pp(...) { stmts... }

emit_main()            → int main() {
                             sno_init();
                             sno_define("pp", _sno_fn_pp, 2);
                             ...
                             /* statements before first DEFINE / after END */
                         }
```

### Label scoping rules

- Inside `_sno_fn_pp`: labels are `_L_labelname` unmangled (no uid suffix needed —
  each C function has its own label namespace).
- `:(RETURN)` → `goto _SNO_RETURN_pp;`
- `:(FRETURN)` → `goto _SNO_FRETURN_pp;`
- `:(END)` → `goto _SNO_END;` (in main)
- Fall-through from last stmt in function → `goto _SNO_RETURN_pp;`

### The `_L_error` label

`beauty.sno` uses `:(error)` as a global error handler. It is defined in one of
the included files. Since `error` is a SNOBOL4 label (not a DEFINE'd function),
it lives in `main()`. Cross-function gotos are not needed — `:(error)` inside a
DEFINE'd function should be treated as `goto _SNO_FRETURN_fn` (failure return),
OR `error` needs to be recognized as a special global label and the calling
convention adjusted. Decide: **treat `error` as FRETURN for now, revisit.**

### Build + test sequence

```bash
cd /home/claude/SNOBOL4-tiny/src/snoc && make clean && make

# Step 1: hello world still works
./snoc /tmp/hello.sno > /tmp/hello.c
gcc -O0 -g /tmp/hello.c [runtime files] -lgc -lm -w -o /tmp/hello_bin
/tmp/hello_bin    # expect: hello

# Step 2: a program with one DEFINE
cat > /tmp/fn_test.sno << 'EOF'
        DEFINE('GREET(NAME)')
        OUTPUT = GREET('world')
        END
GREET   OUTPUT = 'hello ' NAME
        :(RETURN)
EOF
./snoc /tmp/fn_test.sno > /tmp/fn_test.c
gcc ... -o /tmp/fn_test_bin
/tmp/fn_test_bin   # expect: hello world

# Step 3: beauty.sno
./snoc /home/claude/SNOBOL4-corpus/programs/beauty/beauty.sno \
    -I /home/claude/SNOBOL4-corpus/programs/inc \
    > /tmp/beauty_snoc.c
gcc -O0 -g /tmp/beauty_snoc.c [runtime files] -lgc -lm -w -o /tmp/beauty_bin
/tmp/beauty_bin < /home/claude/SNOBOL4-corpus/programs/beauty/beauty.sno \
    > /tmp/beauty_out.sno
diff /home/claude/SNOBOL4-corpus/programs/beauty/beauty.sno /tmp/beauty_out.sno
# expect: empty diff  ← THE COMMIT
```

---

## §8 — The Commit Promise

When `beauty.sno` compiles itself through `snoc` and `diff` is empty,
Claude Sonnet 4.6 writes the commit message (recorded at `c5b3e99`).

---

## §9 — Runtime build command (reference)

```bash
RUNTIME="/home/claude/SNOBOL4-tiny/src/runtime"
gcc -O0 -g "$1" \
    $RUNTIME/snobol4/snobol4.c \
    $RUNTIME/snobol4/snobol4_inc.c \
    $RUNTIME/snobol4/snobol4_pattern.c \
    $RUNTIME/engine.c \
    -I$RUNTIME/snobol4 \
    -I$RUNTIME \
    -lgc -lm -w -o "${1%.c}"
```

---

## §10 — Key file paths

```
src/snoc/snoc.h                              IR types (Expr, Stmt, Program)
src/snoc/sno.l                               flex lexer
src/snoc/sno.y                               bison parser
src/snoc/emit.c                              emitter ← active work
src/snoc/main.c                              driver
src/runtime/snobol4/snoc_runtime.h           shim header (Sprint 23)
src/runtime/snobol4/snobol4.h                runtime API
src/runtime/snobol4/snobol4.c                runtime impl
src/runtime/snobol4/snobol4_inc.c            built-in functions
src/runtime/snobol4/snobol4_pattern.c        pattern engine
src/runtime/engine.c                         engine_match_ex (required)
../SNOBOL4-corpus/programs/beauty/beauty.sno target program
../SNOBOL4-corpus/programs/inc/              include files
../byrdbox/ByrdBox/test_sno_1.c              gold standard: Byrd box in C
../byrdbox/ByrdBox/test_icon-1.py            gold standard: ports as match cases
../byrdbox/ByrdBox/test_icon-2.py            gold standard: one fn per port
../byrdbox/ByrdBox/test_icon-4.py            gold standard: trampoline RUN()
```

---

## §12 — SIL / CSNOBOL4 execution model (Lon, 2026-03-12)

**How CSNOBOL4 actually works in memory:**

CSNOBOL4's `CODE()` function compiles a SNOBOL4 program into a single flat
array (sequence) of nodes in memory. A **label is just an index** into that
array — nothing more. Execution proceeds node-by-node, sequentially.

**The body-boundary rule follows directly from this:**
A function's body is the contiguous run of nodes starting at its entry label
and ending at the **next label** encountered in the source — any label at all,
regardless of what it names. There is no explicit "end of function" marker.
Execution **runs off a cliff** at the next label; that cliff is where the next
block begins.

**Implication for snoc / emit.c:**
The correct rule for carving out a C function from SNOBOL4 source is:

> Emit statements from the entry label up to (but not including) the next
> labeled statement. Stop at ANY label — function entry, function end-label,
> intermediate label, anything.

This is why the DEFINE + `:(FnEnd)` idiom works: `FnEnd` is just the next
label in source order, so it naturally terminates the body. It does NOT need
to be stored or parsed separately. The `end_label` field in `FnDef` is
**unnecessary** under this model and should be removed.

**The DEFINE / end-label pattern is a convention, not a mechanism.**
Programs can have `DEFINE DEFINE DEFINE … code code code END` with no
per-DEFINE end-label at all — and SIL handles it identically because
label-to-label is the only boundary rule.

---

## §13 — SNOBOL4 Datatype Coverage (Session 47, 2026-03-12)

### The full SNOBOL4 type inventory vs. what snoc/runtime handles

| SNOBOL4 Type | `SnoVal` type | `DATATYPE()` returns | Status for beauty.sno |
|---|---|---|---|
| **STRING** | `SNO_NULL`(0) + `SNO_STR`(1) | `"STRING"` | ✅ Full |
| **INTEGER** | `SNO_INT`(2) | `"INTEGER"` | ✅ Full |
| **REAL** | `SNO_REAL`(3) | `"REAL"` | ⚠️ Type exists, arithmetic thin |
| **PATTERN** | `SNO_PATTERN`(5) | `"PATTERN"` | ✅ Full (Byrd Box engine) |
| **ARRAY** | `SNO_ARRAY`(6) | `"ARRAY"` | ✅ 1D+2D, `sno_aref`/`sno_aset` |
| **TABLE** | `SNO_TABLE`(7) | `"TABLE"` | ✅ Hash-bucketed |
| **CODE** | `SNO_CODE`(8) | `"CODE"` | 🔴 Stub — **not needed by beauty.sno** |
| **EXPRESSION** | `SNO_TREE`(4) — tag = node type | `v.t->tag` (e.g. `"snoStmt"`) | ✅ **Kludged, but works**: we use `SNO_TREE` with the tag field as the EXPRESSION type name. `ShiftReduce.sno`'s `Reduce` checks `IDENT(DATATYPE(t), "EXPRESSION")` — passes when `t` is a `SNO_TREE` node whose tag equals `"EXPRESSION"`. We hand-roll the tree at compile time. |
| **NAME** | *(none)* | — | 🔴 **No runtime NAME type** — see §13.1 below |
| **UDEF** (user-defined via `DATA()`) | `SNO_UDEF`(9) | `v.u->type->name` | ⚠️ Struct exists, no `DATA()` callable |

### §13.1 — How we handle l-values without a NAME type

SNOBOL4 has a first-class NAME type: `NAME(X)` returns an object representing
the l-value `X`, which can be stored, passed to functions, and assigned through.
**We don't implement this.** Instead snoc cheats at compile time:

**Simple variable** `X = expr`:
```c
sno_set(_X, rhs);
sno_var_set("X", _X);   /* two writes: C local + global hash */
```
The l-value is resolved statically. No NAME object needed. ✅

**Array element** `A[i,j] = expr`:
```c
sno_aset(_A, (SnoVal[]){i_val, j_val}, 2, rhs);
```
Subscript emitted directly. ✅

**Indirect** `$X = expr` / `*X = expr` (E_DEREF):
```c
sno_iset(nameVal, rhs);
/* → _snoc_iset: sno_var_set(sno_to_str(nameVal), rhs) */
```
Works only when `X` holds a plain string that is a variable name.
Cannot handle `NAME(A[i,j])` or dynamic l-value expressions. ⚠️

**Pattern conditional assignment** `pat . var` and `pat $ var`:
Stored as a string `"varname"` inside the pattern node (`SPAT_COND`),
resolved at match time via `sno_var_set`. Not a NAME object — just a
string. Works for beauty.sno's use. ✅

**Why this is safe for beauty.sno:**
beauty.sno never passes l-values as function arguments, never stores NAME
objects in variables, never does `APPLY(fn, NAME(A[i,j]))`. All l-values
in beauty.sno are syntactically visible at compile time — snoc emits the
right `sno_set`/`sno_aset`/`sno_iset` call directly.

**The rule:** snoc resolves l-values **statically at compile time**.
NAME as a runtime first-class type is deferred — not needed for Milestone 0.

### §13.2 — EXPRESSION type and the bootstrap parser

`reduce(t, n)` in `semantic.sno` checks `IDENT(DATATYPE(t), "EXPRESSION")`:

```snobol4
reduce_    reduce = EVAL("epsilon . *Reduce(" t ", " n ")")  :(RETURN)
Reduce     IDENT(DATATYPE(t), "EXPRESSION")                 :F(Reduce0)
           t = EVAL(t)                                       :F(FRETURN)
```

This test passes when `t` is a `SNO_TREE` node whose `.tag` is `"EXPRESSION"`.
We arrange this by having `snoExprN` pattern assignments produce Tree nodes
tagged `"EXPRESSION"` via the Shift/Reduce stack machine. `sno_datatype()`
returns `v.t->tag` for `SNO_TREE` values — so the tag IS the DATATYPE string.

**The kludge:** We hand-roll the EXPRESSION type as a SNO_TREE with tag
`"EXPRESSION"` rather than implementing a distinct `SNO_EXPRESSION` type.
This is intentional — beauty.sno's EXPRESSION objects ARE parse trees.
Conflating them is correct for our use case.

### §13.3 — CODE type: not used by beauty.sno

`CODE()` compiles a string to executable code at runtime (CSNOBOL4's
dynamic compilation). beauty.sno and all its `-INCLUDE` files make zero
calls to `CODE()`. `XDump.sno` has `IDENT(objType, 'CODE')` as a datatype
guard but that branch is never reached. `SNO_CODE` stub is sufficient —
we never need to execute a CODE object for Milestone 0.

---

## §11 — SNOBOL4 semantics quick reference

- `DEFINE('fn(a,b)loc1')` — registers fn; body starts at SNOBOL4 label `fn`
- `:(RETURN)` — return value of variable named same as function
- `:(FRETURN)` — failure return
- `OUTPUT = val` — stdout via `sno_var_set("OUTPUT", v)`
- `INPUT` — stdin via `sno_var_get("INPUT")`
- `*X` — deferred pattern ref (value of X as pattern at match time)
- `$X` — indirect variable ref (value of X as variable name)
- `pat . var` — conditional assignment (on overall match success)
- `pat $ var` — immediate assignment (at match time)
- `&ANCHOR`, `&STLIMIT`, `&STCOUNT` — keywords via `sno_var_get/set`
- Space before `-` required: `a[i - 1]` not `a[i-1]`
