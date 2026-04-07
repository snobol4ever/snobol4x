# one4all — Open Architecture Decisions

This file is where undecided questions are laid out, argued, and resolved.
Once a decision is made, the conclusion moves to DESIGN.md and the entry
here is marked **DECIDED**.

---

## Decision 1: What language do we write the compiler in?

### The question

The compiler has several distinct components. The language choice does not
have to be the same for all of them. The components are:

| Component | What it does |
|-----------|-------------|
| Parser | Reads SNOBOL4 source, builds AST |
| IR builder | Walks AST, constructs node graph |
| Code generator | Walks IR graph, emits C / ASM / JVM / MSIL |
| Runtime | Executes compiled programs (str_t, match loop, I/O) |
| Test harness | Drives sprints, diffs against oracles |

The runtime almost certainly must be C (or ASM for the inner loop) — it is
the substrate that compiled programs link against. That part is not in
question.

The question is what hosts the parser + IR builder + code generator.

### Candidates

#### A. Python (current)
`ir.py` and `emit_c.py` are already written in Python. The SNOBOL4python
library (our own, on PyPI) is available for pattern matching within the
compiler itself.

- **Pro:** Fast to write. Rich stdlib. ir.py already exists.
- **Pro:** SNOBOL4python lets us use SNOBOL4 patterns *inside* the compiler
  for parsing — dog-fooding the pattern library from day one.
- **Pro:** Shortest path to a working Sprint 2/3/4.
- **Con:** Python is not SNOBOL4. Phase 2 (self-hosting emitter) requires
  a rewrite anyway.
- **Con:** Adds a Python dependency to the build chain.

#### B. SNOBOL4 (on snobol4jvm or snobol4python)
Write the compiler as a SNOBOL4 program from the start. The compiler runs
on snobol4jvm (or CSNOBOL4 / SPITBOL as oracle). The target of compilation
is C-with-gotos.

- **Pro:** True self-hosting from the start — the compiler is a SNOBOL4
  program compiled by a SNOBOL4 engine. Phase 2 is already done.
- **Pro:** Every line of compiler code is a test of our own pattern semantics.
- **Pro:** Snocone (`src/codegen/emit.sno`) becomes the natural host.
- **Con:** Slower to bootstrap early sprints — we must write a SNOBOL4
  parser in SNOBOL4 before we can parse SNOBOL4.
- **Con:** Debugging is harder without Python's stack traces and repls.

#### C. C (the runtime language itself)
Write the compiler as a C program. The compiler reads source and writes
C-with-gotos output.

- **Pro:** Zero additional language dependencies.
- **Pro:** The compiler and runtime share one language — easy to merge them
  later into a single-binary tool.
- **Con:** String handling and pattern matching in C is painful. The whole
  point of SNOBOL4 is that *it* is the right language for this task.
- **Con:** Farthest from self-hosting.

#### D. Hybrid: Python now, SNOBOL4 later (the Forth metacompiler path)
Keep Python for Phases 1–2 (seed kernel + working compiler). Once the
compiler can compile non-trivial SNOBOL4, rewrite the compiler in SNOBOL4
and use it to compile itself. The Python version becomes the bootstrap
oracle — exactly how Forth metacompilers work (Lisp bootstraps Forth,
then Forth replaces the Lisp).

- **Pro:** Best of both worlds. Fast iteration now, self-hosting later.
- **Pro:** This is exactly the lbForth strategy: Lisp metacompiler →
  working kernel → Forth metacompiler replaces the Lisp.
- **Pro:** The Python compiler becomes a permanent validation tool — run
  both compilers on the same input and diff outputs.
- **Con:** Two compilers to maintain during transition.

### Current thinking

Option D (hybrid) matches our sprint plan most naturally. Python drives
Sprints 0–4. The SNOBOL4 emitter (`emit.sno`) is the Sprint 5 deliverable.
After Sprint 5, the Python compiler is frozen as oracle and all new
development happens in SNOBOL4.

**Status: UNDECIDED — needs explicit sign-off before Sprint 5.**

---

## Decision 2: What language does one4all implement first?

### The question

Do we implement full SNOBOL4 from the start, or do we first implement a
smaller, more tightly defined language — and if so, how small?

This breaks into a spectrum:

### Option A: Full SNOBOL4 from the start
Implement the complete SNOBOL4 language: statements, goto-driven control
flow, all primitives, DATA(), DEFINE(), CODE/EVAL, arithmetic, I/O.

- **Pro:** The test corpus (corpus, Gimpel library) applies
  immediately. Every sprint result is directly comparable to SPITBOL.
- **Con:** The full language is large. Too many things to get right before
  the compiler produces a single working program.

### Option B: A minimal pattern-only sublanguage first

The smallest useful one4all program is a single pattern match against
a single input string, producing output. No statements. No control flow.
No variables except OUTPUT.

Concretely: stdin → one pattern match → stdout.

```
POS(0) SPAN('0123456789') $ OUTPUT RPOS(0)
```

This is one pattern, one subject (stdin), one action (write to OUTPUT).
It is complete goal-directed evaluation. It is not SNOBOL4's full statement
model, but it is SNOBOL4's *heart*.

- **Pro:** We already have this in sprint0/sprint1. The path is direct.
- **Pro:** Every primitive is immediately testable against SPITBOL/CSNOBOL4.
- **Con:** Not yet a language — just a pattern engine. No way to name
  subpatterns or chain statements.

### Option C: Two-pattern minimum with mutual recursion (the real minimum)

One pattern alone cannot express recursion. Two patterns that reference each
other can. This is the minimum for a *language* rather than a pattern engine:

```
* Two patterns, mutual recursion, one is "main"
WORD   = SPAN(LETTERS)
MAIN   = POS(0) ARBNO(WORD ' ') RPOS(0)
```

More precisely, the minimum viable language has:
1. **Named pattern definitions** — `NAME = pattern-expression`
2. **Pattern references** — `*NAME` (deferred evaluation, avoids infinite
   recursion at definition time)
3. **One designated entry point** — `MAIN` or the last-defined pattern
4. **One input source** — stdin (the subject string)
5. **One output mechanism** — `$ OUTPUT` capture-and-print

No statements. No goto. No arithmetic. No DATA(). No DEFINE(). No CODE().
Just: define patterns, reference patterns, match stdin, print captures.

This is already Turing-complete for string recognition (it can express any
context-free grammar). It is a clean, small, self-contained language.

- **Pro:** The REF node (already in ir.py) is the only new mechanism needed
  beyond what Sprints 0–4 establish.
- **Pro:** Mutual recursion is the first test that the IR graph (not tree)
  design is correct. This is the right moment to validate it.
- **Pro:** This is genuinely a different, smaller language than SNOBOL4.
  It could have its own name. (one4all is the compiler; the language
  it implements at this stage could be called something else.)
- **Con:** Still not SNOBOL4. Users cannot run existing SNOBOL4 programs.

### Option D: Minimal SNOBOL4 statement model

Add the statement model on top of Option C: a subject string, a pattern,
optional replacement, optional goto. This makes it recognizable SNOBOL4:

```snobol4
LINE = INPUT
LINE  SPAN(LETTERS) $ OUTPUT
```

The minimum additions over Option C:
- Variable assignment (`VAR = expr`)
- The pattern-match statement (`subject  pattern`)
- Unconditional goto (`:(LABEL)`)
- Success/failure branches (`:S(LABEL) :F(LABEL)`)
- `INPUT` / `OUTPUT` special variables
- `END` statement

This is the SNOBOL4 statement model stripped of everything else. It can
run a meaningful subset of real SNOBOL4 programs.

- **Pro:** Now it is SNOBOL4. Programs written for this subset run on
  CSNOBOL4 and SPITBOL unchanged.
- **Con:** Significantly more to implement than Option C.

### Current thinking

The right sequence is **B → C → D**, not a choice between them.

- Sprints 0–4: Option B (single pattern, stdin/stdout, no naming)
- Sprint 5–6: Option C (two patterns, mutual recursion, named definitions,
  REF node validated)
- Sprint 7+: Option D (full statement model, recognizable SNOBOL4)

The key insight is that Option C is where the language becomes *interesting*:
mutual recursion is the first thing that cannot be expressed in any other
pattern language. It validates the graph IR. It is the moment one4all
becomes more than a fancy regex engine.

Option C also answers the naming question: the language at stage C could
legitimately be called **one4all** — a real language with named
patterns and mutual recursion, just without the statement model. Stage D
graduates it to a SNOBOL4 subset.

**Status: DECIDED — 2026-03-10**

**Decision: Expressions first, statements second. The B→C→D sequence is confirmed.**

Rationale: Pattern expressions are the heart of SNOBOL4. The statement model
(subject, replacement, goto) is a wrapper around expression evaluation. Getting
expressions right first — including mutual recursion — means the hard part is
proven before the statement model is layered on top. Every sprint through Stage C
is directly validatable against CSNOBOL4/SPITBOL pattern semantics without the
additional complexity of the statement interpreter.

The naming question (does Stage C deserve its own name?) remains open.

---

## Standing Rule

When a decision is made, update this file (mark DECIDED, record the choice
and rationale), then copy the conclusion to DESIGN.md.
Push both files in the same commit.

---

## Decision 1 (updated): What's already in the ByrdBox C code

Before finalizing Decision 1, it's important to understand what already exists
in the ByrdBox codebase uploaded by Lon. There are three distinct C artifacts:

### What exists in ByrdBox

**`_bootstrap.c`** (126 lines) — a Python C extension (`PyInit__bootstrap`).
It walks a SNOBOL4python PATTERN object tree and dumps it as static C struct
declarations. This is a *serializer*, not an interpreter. Output looks like:

```c
static const PATTERN root_0 = {SPAN, .chars="0123456789"};
static const PATTERN root_1 = {Δ, .s="OUTPUT", &root_0};
```

These structs would need a separate C runtime to execute.

**`test_sno_1.c` through `test_sno_4.c`** — these ARE the compiled output.
They are self-contained C-with-gotos programs using the α/β/γ/ω protocol
directly. No struct interpreter needed — the pattern matching IS the C code.
These are hand-written examples of exactly what one4all's emitter should
produce automatically.

**`transl8r_SNOBOL4.py`** (1,103 lines) — a SNOBOL4→C compiler written in
Python using SNOBOL4python patterns to parse SNOBOL4 source. It has:
- A complete SNOBOL4 lexer (Id, String, Integer, Real, Goto, Label, Stmt)
- A parser producing an AST (Parse, Stmt, Subject, Call, String, Id, ...)
- An `emit()` function that walks the AST and produces C-with-gotos output
- Handles: pattern concatenation (`..`), alternation (`|`), immediate assign
  (`$`), conditional assign (`.`), arithmetic, comparisons, calls, variables,
  gotos, labels, recursive patterns via `*name` deferred references

This is a working SNOBOL4→C compiler in Python. It is not finished (CODE/EVAL,
DATA, DEFINE are not fully implemented) but it handles the full expression
language including mutual recursion.

### What this means for Decision 1

The `transl8r_SNOBOL4.py` + `test_sno_*.c` combination IS the compiler,
already written. The question is not "what language do we write the compiler
in" but rather "how do we take what already exists and make it the one4all
compiler."

**Revised candidate: C + yacc/lex as the front-end**

The transl8r is already Python. The test files show the C output format
exactly. The natural next step Lon identified: rewrite the front-end
(parser) in C using yacc/lex, keeping the C-with-gotos emission model.

This gives:
- `lex` handles tokenization (replaces `Id`, `String`, `Integer`, etc. patterns)
- `yacc` handles grammar (replaces the SNOBOL4python-based parse rules)
- C emission functions (replaces `emit()`, `eStmt()`, `eString()`, etc.)
- The test_sno_*.c files are the gold standard for what the emitter must produce

**Why this is compelling:**
- Zero new language dependencies — the entire toolchain is C
- yacc grammars are well-understood, debuggable, and fast
- The grammar is already implicit in `transl8r_SNOBOL4.py` — porting it to
  yacc is mechanical
- The runtime (str_t, output_t, α/β/γ/ω) is already C and stays C
- The compiler and runtime become a single C program — one binary does both

**The concern:**
- yacc grammars are not self-hosting. The bootstrap path (Phase 2) becomes:
  C compiler → one4all output → eventually a SNOBOL4 program that
  can describe its own grammar. This is further from self-hosting than the
  Python→SNOBOL4 path.
- But: one4all is a *compiler*, not an interpreter. Self-hosting is
  a long-term goal, not Sprint 1. For getting to Stage C (mutual recursion,
  working patterns), C + yacc is the fastest path.

**Status: UNDECIDED — but C + yacc is now the leading candidate given the
existing transl8r.py as specification and the test_sno_*.c files as the
emission gold standard. Needs Lon's sign-off.**

---

## Decision 1 (final analysis): SNOBOL4c.c — the complete picture

`SNOBOL4c.c` (1,064 lines) is a **complete, working SNOBOL4 pattern interpreter
in C**. It is not a sketch. It is production-quality code with a heap allocator,
garbage collector, global variable dictionary, and a full match engine covering
every SNOBOL4python primitive. This changes the Decision 1 analysis substantially.

### What SNOBOL4c.c contains

**The PATTERN struct** (lines 1–60):
All 43 node types defined as integer constants — ABORT, ANY, ARB, ARBNO, BAL,
BREAK, BREAKX, FAIL, FENCE, LEN, POS, RPOS, SPAN, TAB, and the Greek-letter
operators (Σ=SEQ, Π=ALT, σ=LIT, ζ=deferred-ref, δ=capture, λ=lambda, ε=epsilon,
etc.). One unified `PATTERN` struct with a tagged union handles all of them.

**The .h files are compiled pattern data** — not code. They are `#include`d
directly into `SNOBOL4c.c`:
- `BEAD_PATTERN.h`, `BEARDS_PATTERN.h` — simple string matching tests
- `C_PATTERN.h` — recursive arithmetic expression grammar (`x+y*z`)
- `CALC_PATTERN.h` — calculator with stack operations via λ nodes
- `TESTS_PATTERN.h` — identifier and real number patterns

These are exactly the output that `_bootstrap.c` (the Python C extension)
generates from SNOBOL4python pattern objects. The pipeline is:

```
SNOBOL4python pattern expression
    → _bootstrap.c (Python C extension)
    → static const PATTERN NAME_N = {...}; declarations
    → #included into SNOBOL4c.c
    → executed by the MATCH() engine
```

**The heap** (lines 100–360): A bump-pointer allocator with mark-compact GC.
Three stamped object types: STRING, COMMAND, STATE. Handles backtracking
state without touching the C stack.

**The global variable dictionary** (lines 360–430): Hash table mapping pattern
names (strings) to `PATTERN *` pointers. This is how `ζ` (deferred reference)
resolves `*EXPR` → looks up `"EXPR"` in this table at match time.

**The match engine** (lines 430–1000): A single `while (Z.PI)` loop over a
`state_t Z`. The dispatch is `switch (type<<2 | action)` — 4 actions:
PROCEED (α), SUCCESS (γ), FAILURE, RECEDE (β). This is the interpreter
equivalent of the α/β/γ/ω protocol — same four states, heap-allocated instead
of goto-compiled.

Every node type is fully implemented: Σ (SEQ/concatenation), Π (ALT/alternation),
ARBNO, ARB, FENCE, BAL, all primitives, ζ (dynamic pattern reference), δ
(immediate capture to OUTPUT or variable), λ (execute command string), and more.

**The test harness** (`main()`): Registers patterns by name, calls `MATCH()`.
Commented-out test cases cover BEAD, BEARDS, C (expression grammar), CALC
(calculator), ARB, ARBNO, BAL, identifier, real_number, RE_RegEx.

### What this means for Decision 1

**SNOBOL4c.c is the runtime.** It is not a compiler — it interprets the
`PATTERN` struct graph at runtime. But the struct format IS the IR. The
`.h` files ARE compiled pattern data. The interpreter IS the reference
implementation of every node's semantics.

The path to one4all as a compiler now has two routes:

**Route A: Add a front-end to SNOBOL4c.c (C + yacc)**

Add a yacc/lex parser that reads SNOBOL4 source and emits `.h` files
(static PATTERN declarations) instead of interpreting at runtime. The
`MATCH()` engine already works. The `.h` file format is already defined.
A yacc grammar for the expression language can be derived mechanically
from `transl8r_SNOBOL4.py`.

- Fastest path to a working system
- Zero new infrastructure — the runtime is done
- yacc grammar is well-understood
- Self-hosting is deferred (yacc is not SNOBOL4)
- This is the "add a compiler to an interpreter" path Chuck Moore used
  for early Forth systems

**Route B: Use SNOBOL4c.c as the bootstrap interpreter, emit C-with-gotos**

Run the interpreter to parse SNOBOL4 source (writing the parser itself as
SNOBOL4 patterns in `SNOBOL4c.c`), and have the parser *emit* C-with-gotos
(the `test_sno_*.c` format) instead of executing. The interpreter becomes
the compiler's front-end.

- The parser is written in SNOBOL4 — dog-fooding from day one
- The emitted code is the faster α/β/γ/ω goto format
- More complex to set up initially
- But: this IS self-hosting — the language parses itself

### Current recommendation

**Route A first, Route B as Sprint 5.**

Route A gives a working compiler immediately. Route B is the natural Phase 2
(self-hosting emitter) described in BOOTSTRAP.md — but now the bootstrap
interpreter is already written. SNOBOL4c.c is the seed kernel. It is Chuck
Moore's "12 primitives in C."

**Status: UNDECIDED — but Route A (C + yacc front-end to SNOBOL4c.c) is
the leading candidate. The interpreter is done. We need the parser.**

---

## Decision 1 (resolved): Beautiful.sno eliminates the parser problem

### The discovery

`Beautiful.sno` (in snobol4dotnet) contains a complete SNOBOL4 expression
parser written as SNOBOL4 patterns: `snoExpr` through `snoExpr17`, plus
`snoStmt`, `snoLabel`, `snoGoto`, `snoComment`, `snoParse`, `snoCompiland`.
This is the full grammar — 17 precedence levels, all operators, function calls,
subscripts, gotos.

`S4_expression.sno` (the stripped-down version in the same folder) shows exactly
how to use it:

```snobol4
START   LINE = INPUT                               :F(END)
        LINE  POS(0) *snoExpr RPOS(0)             :F(NOTOK)
        OUTPUT = "Valid SNOBOL4"                   :(START)
NOTOK   OUTPUT = "Syntax ERROR"                   :(START)
END
```

Read a line from stdin. Match it against `*snoExpr`. Print result to stdout.
That is the entire stdin→stdout expression validator. Five lines.

### What this means

Route A (yacc/lex front-end) is **obsolete**. We do not need yacc. We do not
need to write a grammar. The grammar is already written — in SNOBOL4 — and
`SNOBOL4c.c` already knows how to execute SNOBOL4 patterns.

The plan:

1. **Extract `snoExpr` and all sub-patterns from `Beautiful.sno`** into a
   new file: `src/patterns/SNOBOL4_EXPRESSION_PATTERN.h`. This is mechanical
   work — the same transformation `_bootstrap.c` does: walk the pattern
   structure, emit `static const PATTERN` declarations.

2. **`#include "SNOBOL4_EXPRESSION_PATTERN.h"` in `SNOBOL4c.c`** alongside
   the existing test patterns.

3. **Add 5 lines to `main()`** mirroring `S4_expression.sno`: register
   `snoExpr` in the globals table, read stdin line by line, call `MATCH()`.

4. **`SNOBOL4c.c` now reads stdin and parses SNOBOL4 expressions.** No new
   language infrastructure. No yacc. No lex. The interpreter is the parser.

### The beauty

Route A and Route B have **collapsed into the same route**:

- Route A was: write a parser in a new language (yacc), have it emit `.h`
  files for the interpreter to execute.
- Route B was: write the parser as SNOBOL4 patterns inside `SNOBOL4c.c`.
- **Actual route:** The parser is already written as SNOBOL4 patterns
  (`Beautiful.sno`). Hard-code it as a `.h` file. Done.

This is exactly the Forth bootstrap move: the seed kernel (`SNOBOL4c.c`)
executes pattern data (`.h` files). The pattern data for the SNOBOL4 parser
already exists. We just need to serialize it into the `.h` format and include
it. The language parses itself from Sprint 1, not Sprint 8.

### The `λ` bridge

`Beautiful.sno` uses `nPush()`, `nInc()`, `nPop()` and string-quoted
Reduce actions to build a parse tree (the `Shift`/`Reduce`/`Pop` nodes in
`SNOBOL4c.c`). The full beautifier pipeline is:

```
stdin → snoExpr (pattern match, builds tree via Shift/Reduce)
      → pp() (pretty-print the tree back to SNOBOL4)
      → stdout
```

For one4all we don't need `pp()`. We need the match step to instead
emit IR nodes (or C-with-gotos directly). That is the `λ` bridge: replace
the `Reduce` actions that build a pretty-print tree with `λ` actions that
emit IR. `Beautiful.sno` shows exactly what the parse tree looks like —
`snoStmt` nodes with 7 children (label, subject, pattern, `=`, replacement,
goto1, goto2). That *is* the IR shape for Stage D (full statements).

### Revised sprint plan

| Sprint | What | How |
|--------|------|-----|
| 0–4 | Pattern primitives, concat, alt, capture | existing `SNOBOL4c.c` |
| 5 | **`SNOBOL4_EXPRESSION_PATTERN.h`** + stdin loop | serialize `snoExpr` from `Beautiful.sno` |
| 6 | Replace Reduce actions with λ IR emitters | edit the `.h` file |
| 7 | Full statement model (`snoStmt`, labels, gotos) | `snoStmt`/`snoParse` from `Beautiful.sno` |
| 8 | Bootstrap closure: compiler compiles itself | oracle diff against CSNOBOL4/SPITBOL |

**Status: DECIDED. No yacc. No new parser. Serialize `Beautiful.sno` patterns
into a `.h` file and include it. The interpreter is the compiler.**

---

## Decision 8: Polyglot stdin — EDN and alternate grammars via Alt root pattern

### The Proposal

The root pattern passed to `MATCH()` in `SNOBOL4c.c`'s stdin loop need not be a
single grammar. It can be:

```c
root = ALT(snoStmt, ALT(ednExpr, ALT(incStmt, ...)));
```

The backtracking engine dispatches between grammars automatically. No format
detection code. No switch. Polyglot parsing is a consequence of Π already existing.

### EDN as First Alt Candidate

EDN (Extensible Data Notation) is the natural first addition because:
1. snobol4jvm uses Clojure/EDN for its internal data structures.
2. A one4all that reads EDN can consume snobol4jvm IR directly.
3. EDN grammar is small — maps, vectors, lists, keywords, strings, numbers.
   The entire grammar fits in ~20 SNOBOL4 pattern definitions.

### INC Files

`.inc` files (include/macro definitions used in SNOBOL4 assembler dialects and
some SPITBOL distributions) are another natural arm — they have a simple line-oriented
grammar that's a proper subset of what the existing parsers already handle.

### The Architecture Point

This decision is not about adding features. It reveals that `SNOBOL4_EXPRESSION_PATTERN.h`
is a **loadable grammar slot**, not a fixed parser. one4all is a compiler whose
input language is determined at compile time by which `.h` files are included.

Adding `EDN_PATTERN.h` to the `#include` chain and wrapping the root in `ALT()` makes
one4all a polyglot compiler with zero new C infrastructure.

**Status: DECIDED — 2026-03-10**

**Decision: Implement EDN_PATTERN.h as the Sprint 7 deliverable (after REF/mutual recursion
is validated in Sprint 6). INC support follows as Sprint 8. The root Alt chain is the
architecture — each new language is one more arm, one more `.h` file.**

Rationale: The insight that Alt IS the dispatcher is architecturally significant and
should be locked in now. It changes how we think about what one4all *is*:
not a SNOBOL4 compiler but a grammar-driven compiler compiler. Every decision
downstream should be made with this in mind.

---

## Decision 9: NAME type and l-value strategy (Session 47, 2026-03-12)

### The question

SNOBOL4 has a first-class NAME type. `NAME(X)` returns an object representing
the l-value of `X`. Functions can receive NAME arguments and assign through
them. Does snoc need to implement NAME as a runtime type?

### Analysis

beauty.sno never:
- passes l-values as function arguments
- stores NAME objects in variables
- uses `APPLY(fn, NAME(A[i,j]))` or similar dynamic l-value passing

All l-values in beauty.sno are syntactically visible at the call site.
snoc can always determine statically whether an assignment target is a simple
variable, array subscript, or indirect string — and emit the right C call.

### Decision: DECIDED — compile-time l-value resolution, no NAME type

snoc resolves l-values at **compile time**. No `SNO_NAME` type is needed
for Milestone 0. The four cases are:

1. `X = rhs` → `sno_set(_X, rhs); sno_var_set("X", _X)`
2. `A[i,j] = rhs` → `sno_aset(_A, keys, n, rhs)`
3. `$X = rhs` → `sno_iset(X_val, rhs)` (string-as-varname)
4. `pat . var` → SPAT_COND node stores varname string, resolved at match time

NAME as a runtime first-class type is deferred until a program requires it.

---

## Decision 10: EXPRESSION type and SNO_TREE (Session 47, 2026-03-12)

### The question

SNOBOL4 has an EXPRESSION type for unevaluated expressions. beauty.sno's
`ShiftReduce.sno` tests `IDENT(DATATYPE(t), "EXPRESSION")`. How do we
satisfy this without implementing a distinct EXPRESSION type?

### Decision: DECIDED — SNO_TREE with tag = "EXPRESSION"

`sno_datatype()` returns `v.t->tag` for `SNO_TREE`. We arrange that
expression parser Tree nodes are tagged `"EXPRESSION"`. The test
`IDENT(DATATYPE(t), "EXPRESSION")` passes naturally.

No semantic gap: beauty.sno's EXPRESSION objects ARE parse trees.
The kludge is correct by construction.

---

## Decision 11: CODE type (Session 47, 2026-03-12)

### Decision: DECIDED — stub only, not needed for Milestone 0

beauty.sno and all 19 `-INCLUDE` files it uses make **zero calls** to `CODE()`.
`SNO_CODE` stays as a type tag stub. Implementation deferred indefinitely.

---

## Decision 12: Smoke test — per-statement snoCommand match (Session 50, 2026-03-12)

### The question

How do we verify the grammar (snoCommand pattern) is working correctly?

### Analysis (Session 50)

Key architectural invariant confirmed: **if you strip all `.` and `$` captures
from the grammar patterns, the structural pattern WILL match all beauty.sno
statements.** This was established during the bootstrap phase.

Therefore: the cheapest, most diagnostic smoke test is to match individual
SNOBOL4 statements against `snoCommand` in isolation. If any statement fails,
the grammar is broken (not the captures, not the shift-reduce stack).

### Decision: DECIDED — mandatory smoke test before Milestone 0

Before claiming Milestone 0 complete, the following must pass:

1. `beauty_full_bin` built with 0 gcc errors
2. For a representative set of beauty.sno statement types (assignment, pattern
   match, DEFINE, function call, labeled goto), each matches `snoCommand`
3. The full self-beautification produces empty diff vs oracle

File: `test/smoke/test_snoCommand_match.sh`

This replaces the obsolete Python-based sprint22 oracle as the primary
end-to-end smoke test for the C compiler pipeline.

---

## Decision 13: Test suite is obsolete — replace with shell smoke tests (Session 50, 2026-03-12)

### The situation

`test/sprint*/` contain Python-based tests using `sno_parser` / `emit_c_stmt`
— the old Python snoc pipeline. The current compiler is pure C (`snoc`).
These tests do not run against the current compiler at all.

### Decision: DECIDED — new smoke tests in `test/smoke/`

Shell scripts that exercise the C snoc pipeline directly:
- `build_beauty.sh` — compile beauty.sno → C → binary, 0 gcc errors
- `test_snoCommand_match.sh` — match sample statements against snoCommand
- `test_self_beautify.sh` — self-beautification diff vs oracle (Milestone 0)

Old `test/sprint*/` retained for historical reference but not run in CI.
