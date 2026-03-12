# SNOBOL4-tiny — Sprint Plan

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

## §6 — The Execution Model: Statement-Level Exception Handling

### The Byrd box insight applied to statements

The `test_icon*.py` and `test_sno_1.c` files in ByrdBox establish the gold standard:
**each node in the compiled graph has exactly four ports: α (enter), β (resume/retry),
γ (succeed), ω (fail)**. This is the Byrd Box model — see `doc/DESIGN.md`.

For SNOBOL4 *statements*, the same four-port model applies. Each statement is a
box. SUCCESS and FAILURE are not thrown/caught with C exceptions — they are
**goto edges** between ports. The granularity is exactly one statement per box.

### The two-level architecture

**Level 1 — Statement boxes (current model, flat C gotos)**

Each SNOBOL4 statement compiles to a labeled region with:

```c
_stmt_N_alpha:   /* enter — evaluate subject, attempt pattern match */
    ...
    if (match_failed) goto _stmt_N_omega;
    ...
    goto _stmt_N_gamma;

_stmt_N_gamma:   /* success path — execute replacement, follow :S() goto */
    ...
    goto _stmt_M_alpha;   /* next stmt or :S(label) */

_stmt_N_omega:   /* failure path — follow :F() goto */
    ...
    goto _stmt_K_alpha;   /* :F(label) or fall-through */
```

No C `try`/`catch`. No `setjmp`/`longjmp`. The SUCCESS and FAILURE branches ARE
the goto wiring — exactly as in `test_sno_1.c` and `test_icon.c`.

This is what the current `emit.c` approximates with `_SNO_NEXT_N` labels. The
fix is to make the alpha/gamma/omega structure explicit and symmetric.

**Level 2 — Function boxes (the Sprint 24 fix)**

Each `DEFINE('fn(args)locals')` in SNOBOL4 compiles to a **separate C function**:

```c
SnoVal _sno_fn_pp(SnoVal *args, int nargs) {
    /* local variable declarations */
    static SnoVal _a = {0}, _b = {0};  /* args */
    static SnoVal _loc = {0};          /* locals */

    /* statement boxes for the body of pp */
    _stmt_0_alpha: ...
    _stmt_0_gamma: ...
    _stmt_0_omega: ...

    _SNO_RETURN_pp:   return sno_get(_pp);   /* :(RETURN) lands here */
    _SNO_FRETURN_pp:  return SNO_FAIL_VAL;   /* :(FRETURN) lands here */
}
```

`main()` becomes the top-level "program function" — it holds only the statements
that precede the first DEFINE and the END label.

Duplicate label problem: **solved** — each C function has its own label namespace.

### Why this is better than both alternatives

| Approach | Duplicate labels | RETURN/FRETURN | Locals scope | Notes |
|----------|-----------------|----------------|--------------|-------|
| Flat main() | ❌ duplicates | ❌ undefined | ❌ shared | Current broken state |
| Flat with uid mangling | ✅ | ✅ | ~ok | Ugly, fragile, limits optimization |
| **Function-per-DEFINE** | ✅ | ✅ | ✅ | **The right model** |

### The optimization angle

Statement-level granularity (Level 1) is **the baseline semantics** — it is how
SNOBOL4 is supposed to work and is required for correctness.

Function-level C functions (Level 2) are an **architectural consequence** of
proper scoping — they happen to also be where C compiler optimizations apply.
The C compiler sees a real function, can inline it, allocate registers, etc.

Future: once both levels work, statement boxes *within* a function can be
flattened further using the `test_icon-2.py` model (one C function per port:
`pp_alpha`, `pp_beta`, `pp_gamma`, `pp_omega`) as a micro-optimization for
hot inner loops. This is **not needed now** — get correctness first.

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
