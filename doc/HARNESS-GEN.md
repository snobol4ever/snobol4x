# HARNESS-GEN.md — Grammar-Driven Semantic Test Generator

*Session G-10 s1, 2026-03-31. Lives in `one4all/doc/`. Code spans `harness/` and `one4all/`.*

---

## Purpose

Generate SNOBOL4 programs that are:
1. **Syntactically valid** — no parse errors
2. **Semantically interesting** — at least one variable changes value
3. **Structurally diverse** — one representative per distinct expression *shape*

Feed the resulting `.sno` + `.ref` pairs directly into the existing crosscheck runner.
This replaces hand-written corpus entries for the arithmetic/assignment fragment.

---

## Scope (Phase 1)

- **Language:** SNOBOL4 arithmetic expressions only
- **Statement forms:** assignment (`I = expr`) and output (`OUTPUT = expr`)
- **Fragment:** 4-operator grammar — `+`, `-`, `*`, `/` with unary `+`/`-`
- **Repos:** generator in `harness/adapters/tiny/Expressions.py` (done);
  oracle IPC and suite runner in `harness/gen/`; test output to `one4all/test/generated/`

---

## Variable Pools (naming convention)

| Pool | Names | Type | Notes |
|------|-------|------|-------|
| Integer | `i j k l m n` | numeric | Preamble assigns small int values |
| Pattern | `p q r` | pattern | Reserved — Phase 2 |
| String | `s t` | string | Reserved — Phase 2 |
| Untyped/any | `u v w x y z` | null initially | Tests null-handling, coercion paths |

The preamble generator inspects which variables appear in an expression and emits
initialisation statements for the integer pool only (Phase 1):

```snobol4
    I = 3
    J = 7
    K = 2
```

`u..z` are left uninitialised — they are null/unset, which exercises SNOBOL4's
coercion of null to zero in arithmetic contexts.

---

## SPITBOL Oracle IPC — synchronous pipe protocol

**Design principle: synchronous.** One request → one response before the next request
is sent. Learned from Monitor: async FIFO multiplexing is fragile. Here the oracle is
a single long-lived SPITBOL process; Python drives it synchronously over stdin/stdout.

### Protocol

```
Python → SPITBOL stdin:
    <preamble lines>\n
    <statement line>\n
    __EVAL__\n

SPITBOL → Python stdout:
    <DUMP output lines>
    __DONE__\n
```

`__EVAL__` triggers a `&DUMP` + flush; `__DONE__` is the sentinel Python polls for.
One blocking `readline()` loop on the Python side — no select/poll needed.

### SPITBOL driver skeleton (`harness/oracle/spitbol_driver.sno`)

```snobol4
*   spitbol_driver.sno — persistent oracle process for harness IPC
*   Run: spitbol spitbol_driver.sno
*   Reads statements from INPUT one line at a time.
*   On __EVAL__: dumps all variables, prints __DONE__, flushes.

LOOP    LINE = INPUT                              :F(END)
        EQ(LINE, '__EVAL__')                      :S(DUMP)
        CODE(LINE)()                              :F(ERR)
        :(LOOP)
DUMP    &DUMP = 1
        OUTPUT = '__DONE__'
        :(LOOP)
ERR     OUTPUT = '__ERROR__ ' &ERRTEXT
        OUTPUT = '__DONE__'
        :(LOOP)
END
```

*Note: `CODE()` compiles and executes a string as a SNOBOL4 statement.
Error handling resets state and sends `__DONE__` so Python never hangs.*

### Python IPC wrapper (`harness/oracle/spitbol_ipc.py`)

```python
class SpitbolOracle:
    def __init__(self, spitbol='spitbol', driver='harness/oracle/spitbol_driver.sno'):
        self.proc = subprocess.Popen(
            [spitbol, driver],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1)   # line-buffered

    def run(self, preamble: list[str], statement: str) -> dict:
        """Send preamble + statement, return parsed DUMP as {varname: value}."""
        for line in preamble:
            self.proc.stdin.write(line + '\n')
        self.proc.stdin.write(statement + '\n')
        self.proc.stdin.write('__EVAL__\n')
        self.proc.stdin.flush()
        lines = []
        while True:
            line = self.proc.stdout.readline().rstrip('\n')
            if line == '__DONE__':
                break
            lines.append(line)
        return parse_dump(lines)

    def close(self):
        self.proc.stdin.close()
        self.proc.wait()
```

---

## Result Classes

Every program sent to the oracle yields exactly one class:

| Class | Condition | Keep? |
|-------|-----------|-------|
| `WELL_BEHAVED` | DUMP returned; ≥1 variable changed from initial state | ✅ yes |
| `NO_EFFECT` | DUMP returned; all variables unchanged | skip |
| `ERROR` | `__ERROR__` line received | skip (log) |
| `HANG` | readline timeout exceeded | skip (log, kill+restart) |
| `CRASH` | process died | restart (log) |

Only `WELL_BEHAVED` programs are harvested for the test suite.

---

## Shape Extraction

Two expressions have the same **shape** if they have identical operator tree structure,
ignoring which specific variable names or integer literals appear.

Shape is computed from the AST tuple:

```python
def shape(tree):
    if isinstance(tree, int):   return 'INT'
    if isinstance(tree, str):   return 'VAR'
    op = tree[0]
    return (op,) + tuple(shape(c) for c in tree[1:])
```

Examples:
- `i + j`   → `('+', 'VAR', 'VAR')`
- `3 + k`   → `('+', 'INT', 'VAR')`  ← different shape from above
- `i + j`   and `k + l` → same shape `('+', 'VAR', 'VAR')` — one is enough
- `i * j + k` → `('+', ('*', 'VAR', 'VAR'), 'VAR')`

The `ShapeCatalog` keeps one representative `(expr_str, preamble, expected_dump)` per shape.

---

## Pipeline (`harness/gen/gen_suite.py`)

```
exhaust_expressions(max_size=N)        # Expressions.py — yields (str, tree) pairs
  for each (expr_str, tree):
    vars = extract_vars(tree)          # which variables appear
    preamble = gen_preamble(vars)      # e.g. ["I = 3", "J = 7"]
    stmt = as_assign(vars[0], expr_str) or as_output(expr_str)
    result_class, dump = oracle.run(preamble, stmt)
    if result_class != WELL_BEHAVED: continue
    sh = shape(tree)
    if sh in catalog: continue        # already have this shape
    catalog.add(sh, expr_str, preamble, dump)

emit_test_files(catalog, outdir='one4all/test/generated/')
```

Each emitted test is a `.sno` file + `.ref` file:

```snobol4
*   generated/arith_add_var_var.sno
    I = 3
    J = 7
    OUTPUT = I + J
END
```
```
*   generated/arith_add_var_var.ref
10
```

These drop directly into `crosscheck/crosscheck.sh`.

---

## Milestones

| ID | Action | Deliverable | Gate |
|----|--------|-------------|------|
| **M-H0** | SPITBOL IPC — driver + Python wrapper | `harness/oracle/spitbol_driver.sno`, `harness/oracle/spitbol_ipc.py` | 1000 round-trips/sec on `I = 1 + 2` |
| **M-H1** | Result classifier | `harness/oracle/classifier.py` | All 5 classes produced by crafted inputs |
| **M-H2** | Shape extractor + catalog | `harness/oracle/shape.py` | `i+j` and `k+l` collapse to same shape; `3+j` distinct |
| **M-H3** | Preamble generator | `harness/gen/preamble.py` | Correct SNOBOL4 for `i..n` pool; nulls for `u..z` |
| **M-H4** | `exhaust_expressions` yields `(str, tree)` pairs | update `Expressions.py` | Smoke test: size=2 yields 30k pairs with trees |
| **M-H5** | Full pipeline `gen_suite.py` | `harness/gen/gen_suite.py` | Runs end-to-end for `max_size=2`; emits files to `one4all/test/generated/` |
| **M-H6** | Integration with crosscheck | no new code | `crosscheck.sh --engine spitbol` passes all generated tests |
| **M-H7** | Run against one4all x86 | no new code | zero failures at `max_size=2`; divergences logged with shape |

**Phase 2 (deferred):** extend to `p..r` pattern pool and `s,t` string pool.
**Phase 2 (deferred):** random engine (`rand_expressions`) feeds same pipeline for larger coverage beyond exhaustive depth.

---

## Open Questions

1. **`CODE()` availability** — verify SPITBOL supports `CODE(stmt)()`. If not, the driver needs a different eval mechanism (write to temp file, `-i` input mode, etc.).
2. **DUMP format** — parse SPITBOL `&DUMP=1` output format into `{varname: value}` dict. Need one sample run to nail the exact format.
3. **`max_size` for Phase 1** — size=2 gives 30k expressions. After shape dedup, expect O(100) unique shapes. Size=3 will be tractable. Size=4+ may need the random engine instead.
4. **Hang timeout** — what's the right per-statement timeout? Monitor uses 10s inter-event. For simple arithmetic, 1s is generous.
5. **State reset between tests** — SPITBOL driver must reset all variables between calls. Either restart the process per batch or send explicit `I =` null-assigns in the preamble.

---

*HARNESS-GEN.md = design + milestones only. No sprint content. Update milestones table as work proceeds.*
