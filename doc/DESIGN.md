# SNOBOL4-tiny — Design Notes

## The Byrd Box Model

The alpha/beta/gamma/omega protocol comes from Peter Byrd's 1980 box model for
Prolog execution, adapted here for SNOBOL4/Icon-style goal-directed evaluation.

Each compiled expression node owns exactly four labeled entry points:

- **alpha** — Enter fresh. Initialize node state. Attempt first solution.
- **beta**  — Resume. The downstream consumer failed; try next solution.
- **gamma** — Succeed. Wire to the next node's alpha (or match success).
- **omega** — Fail. Wire to the enclosing choice point's beta (or match failure).

Wiring rules:

```
Concatenation (P Q):
    P_gamma -> Q_alpha      (P succeeded, start Q)
    Q_omega -> P_beta       (Q failed, backtrack into P)

Alternation (P | Q):
    P_omega -> Q_alpha      (P failed, try Q)
    Q_omega -> outer_omega  (Q failed, propagate up)
    P_gamma -> outer_gamma
    Q_gamma -> outer_gamma

ARBNO(P):
    enter_alpha -> P_alpha  (try P once more)
    P_gamma -> enter_alpha  (P matched, loop back)
    P_omega -> outer_gamma  (P failed, exit with accumulated match)
    outer_beta -> P_beta    (downstream failed, undo last P)
```

## IR Design

The IR is a named flat table of nodes (a graph, not a tree) to support
recursive patterns like:

```snobol4
EXPR = TERM ('+' TERM)*
```

Cycles are handled via REF nodes. At parse time, a forward reference emits
REF("EXPR"). At codegen time, REF("EXPR") emits a jump to EXPR_alpha.

Node types:

| Node       | alpha behavior                  | beta behavior              |
|------------|---------------------------------|----------------------------|
| LIT(s)     | Match literal s at cursor       | Restore cursor, fail       |
| ANY(cs)    | Match one char in charset cs    | Restore cursor, fail       |
| SPAN(cs)   | Match 1+ chars in cs            | Backtrack one char at a time |
| BREAK(cs)  | Match 0+ chars not in cs        | Deterministic — fail       |
| LEN(n)     | Advance cursor by n             | Restore cursor, fail       |
| POS(n)     | Assert cursor == n              | Fail                       |
| RPOS(n)    | Assert cursor == len-n          | Fail                       |
| ARB        | Try 0 chars, then 1, 2, ...     | Advance one char, retry    |
| ARBNO(P)   | Try 0 repetitions, then 1, ...  | Undo last P                |
| ALT(P,Q)   | Try P                           | On P_omega, try Q          |
| CAT(P,Q)   | P then Q                        | On Q_omega, backtrack P    |
| ASSIGN(P,V)| Match P, assign V on gamma      | Pass beta through to P     |
| REF(name)  | jmp name_alpha                  | jmp name_beta              |

## Static Allocation

All working state for pattern nodes is allocated statically at compile time.
Each node instance gets a unique name prefix: lit7_, span3_, arb12_, etc.

For .bss (x86-64 NASM):

```nasm
section .bss
    lit7_saved_cursor:  resq 1
    span3_saved_cursor: resq 1
    span3_delta:        resq 1
    arb12_len:          resq 1
```

For recursive patterns, temporaries are arrays indexed by a depth counter:

```c
typedef struct { int64_t saved_cursor; } arbno5_t;
static arbno5_t arbno5_stack[64];
static int      arbno5_depth = 0;
```

CODE/EVAL dynamic patterns use heap allocation (two-tier: static fast path
for compiled patterns, heap for runtime-generated ones).

## Code Generation Strategy

Template expansion: each node type has a parameterized C (or ASM) template.

Template for LIT(s) in C-with-gotos:

```c
/* LIT("{s}") — node {id} */
{id}_alpha:
    if (cursor + {len} > subject_len) goto {id}_omega;
    if (memcmp(subject + cursor, "{s}", {len}) != 0) goto {id}_omega;
    {id}_saved_cursor = cursor;
    cursor += {len};
    goto {gamma};
{id}_beta:
    cursor = {id}_saved_cursor;
    goto {omega};
```

The emitter substitutes {s}, {len}, {id}, {gamma}, {omega}.
No AST walking at runtime. No indirect dispatch.

## Multi-Target IR

The same node graph drives all three backends:

- x86-64 ASM (NASM): jmp/je/jne, rsi = cursor, rdi = subject ptr
- JVM bytecode (ASM library): GOTO, IF_ICMPNE, locals for cursor/saved
- MSIL (ILGenerator): Br, Bne_Un, locals

## CODE / EVAL

CODE(s) and EVAL(s) are re-entrant calls into the same compiler pipeline.
No interpreter fallback. No mode switch.
For x86-64: compiled code lands in mmap(PROT_EXEC).
For JVM: ClassLoader.defineClass().
For MSIL: DynamicMethod.

---

## The Forth Analogy — Why This Architecture

SNOBOL4-tiny's architecture is deliberately modeled on the Forth kernel/dictionary
split. Forth's power comes from having an irreducibly small native kernel (~12
primitives) and then building everything else in the language itself. The same
discipline applies here.

### The NEXT Equivalent

In Forth, NEXT is the 3-instruction heartbeat of the entire system:

```asm
NEXT: lw  W, (IP)    ; fetch next word pointer
      add IP, 4      ; advance instruction pointer  
      jmp (W)        ; jump to code
```

SPITBOL uses an equivalent: `succp` (3 instructions: load pthen, load pcode,
jmp). Every pattern node pays this cost at runtime.

SNOBOL4-tiny pays **zero**. The α/β/γ/ω wiring is baked into the compiled
output as static gotos. There is no dispatch loop — the wiring IS the execution.
This is the fundamental speed advantage over SPITBOL's threaded model.

The price: the graph must be fully known at compile time. CODE/EVAL is the
escape hatch — it re-enters the compiler and extends the graph at runtime,
exactly as Forth's `:` extends the dictionary.

### Primitive Minimality

Before adding any new primitive node, ask: can it be expressed as CAT, ALT,
or ARBNO of existing primitives? If yes — don't add it. Write the derivation.

ARBNO is *derivable* from ARB + CAT + ALT once those work. TAB(n) is
derivable from POS(n). These are not primitives — they are library words,
written in SNOBOL4 the same way Forth's higher words are written in Forth.

Irreducible primitives (nothing smaller can express them):
LIT, ANY, POS, RPOS, LEN, SPAN, BREAK, ARB.

Derivable (write in SNOBOL4, not as C templates):
ARBNO, TAB, RTAB, and most compound patterns.

See `doc/BOOTSTRAP.md` for the full three-phase bootstrap strategy.

---

## Open Decisions

See `doc/DECISIONS.md` for the two foundational questions. Decision 2
(scope/sequencing) is now settled — see below.

## Key Design Decisions (Settled)

### Expressions first, statements second (decided 2026-03-10)

SNOBOL4-tiny implements pattern expressions before the SNOBOL4 statement
model. Stages:

- **Stage B** (Sprints 0–4): single pattern, stdin/stdout, no naming
- **Stage C** (Sprints 5–6): named patterns, mutual recursion, REF node —
  the minimum for a real language
- **Stage D** (Sprint 7+): SNOBOL4 statement model (variables, goto,
  INPUT/OUTPUT, END)

Rationale: expressions are the hard part. The statement model is a wrapper.
Getting the expression semantics — including mutual recursion — proven first
means every subsequent layer builds on a validated foundation.

Decision 1 (compiler implementation language) remains open — see
`doc/DECISIONS.md`.

---

## The Interpreter/Compiler Duality (key discovery)

`SNOBOL4c.c` is a complete SNOBOL4 pattern interpreter in C. Its match engine
uses four actions:

```c
#define PROCEED 0   // ↓  enter a node fresh
#define SUCCESS 1   // ↑  node succeeded, return up
#define FAILURE 2   // ↗  node failed, backtrack
#define RECEDE  3   // ↖  downstream failed, retry
```

These are **the same four states as α/β/γ/ω** — not by coincidence. Both
are implementations of the Byrd Box model. The difference is execution strategy:

| | SNOBOL4c.c interpreter | SNOBOL4-tiny compiler |
|---|---|---|
| Dispatch | `switch(type<<2\|action)` at runtime | Static `goto` labels baked into C output |
| State | `state_t Z` on heap | Named static variables per node |
| Backtrack stack | `Ω_push/Ω_pop` (heap-allocated tracks array) | Implicit in goto graph structure |
| Pattern data | `static const PATTERN` structs (`.h` files) | Inlined C-with-gotos (`.c` files) |
| Recursion | `psi` stack of saved states on heap | Static arrays with depth index |
| Speed | One `switch` dispatch per node per action | Zero dispatch — goto IS execution |

The `.h` files (`BEAD_PATTERN.h`, `C_PATTERN.h`, `CALC_PATTERN.h`, etc.) are
**compiled pattern data** — the output of `_bootstrap.c` (a Python C extension
that walks SNOBOL4python pattern objects and dumps them as C struct declarations).
They are `#include`d directly into `SNOBOL4c.c`.

The `test_sno_*.c` files are what a SNOBOL4-tiny **compiler** emits instead —
the same patterns, but as inlined C-with-gotos rather than interpreted structs.

### The two routes from here

**Route A — Interpreter + yacc front-end:**
Add a yacc/lex parser to `SNOBOL4c.c` that reads SNOBOL4 source and emits
`.h` files (static PATTERN struct declarations). The interpreter executes them.
Fastest path to a working system. Self-hosting deferred.

**Route B — Interpreter parses itself, emits compiled output:**
Write the parser as SNOBOL4 patterns inside `SNOBOL4c.c`. The parser matches
SNOBOL4 source text and emits C-with-gotos (the `test_sno_*.c` format).
The interpreter is the compiler's front-end. Self-hosting from day one.
This is the Forth move: the language parses itself.

**Plan: Route A now (Sprint 0–4), Route B as Sprint 5 (Phase 2 bootstrap).**
`SNOBOL4c.c` is the seed kernel — the "12 primitives in C" of our Forth analogy.
The interpreter is done. What remains is the parser.

---

## Beautiful.sno: the parser is already written

`Beautiful.sno` (SNOBOL4-dotnet repo, `Snobol4/Test Files/Beautiful/`) contains
a complete SNOBOL4 expression and statement parser written as SNOBOL4 patterns.
This resolves the "Route A vs Route B" question entirely.

### The pattern hierarchy (from Beautiful.sno)

```
snoExpr        → snoExpr0 (assignment =)
snoExpr0       → snoExpr1 (conditional ?)
snoExpr1       → snoExpr2 (and &)
snoExpr2       → snoExpr3 (alternation |)
snoExpr3       → snoExpr4 (concatenation, space-separated)
snoExpr4       → snoExpr5 (cursor @)
snoExpr5       → snoExpr6 (arithmetic + -)
snoExpr6       → snoExpr7 (string #)
snoExpr7       → snoExpr8 (division /)
snoExpr8       → snoExpr9 (multiplication *)
snoExpr9       → snoExpr10 (remainder %)
snoExpr10      → snoExpr11 (exponentiation ^ ! **)
snoExpr11      → snoExpr12 (pattern ops $ .)
snoExpr12      → snoExpr13 (complement ~)
snoExpr13      → snoExpr14 (unary prefix ops)
snoExpr14      → snoExpr15 (atoms, subscripts)
snoExpr15      → snoExpr17 (function calls, parens, literals)
```

Plus: `snoStmt` (7-child: label/subject/pattern/=/replacement/goto1/goto2),
`snoLabel`, `snoGoto`, `snoComment`, `snoControl`, `snoParse`, `snoCompiland`.

### The stdin→stdout bridge (S4_expression.sno)

The 5-line main pattern from `S4_expression.sno`:

```snobol4
START   LINE = INPUT                               :F(END)
        LINE  POS(0) *snoExpr RPOS(0)             :F(NOTOK)
        OUTPUT = "Valid SNOBOL4"                   :(START)
NOTOK   OUTPUT = "Syntax ERROR"                   :(START)
END
```

Translated to C (`main()` addition after registering patterns):

```c
const char * line;
while ((line = read_line(stdin)) != NULL) {
    assign_ptr("snoExpr", &snoExpr_0);
    MATCH("snoExpr", line);
}
```

### The serialization step (Sprint 5)

Extract all `snoExpr*` patterns from `Beautiful.sno` and serialize them into
`src/patterns/SNOBOL4_EXPRESSION_PATTERN.h` — the same static PATTERN struct
format used by `BEAD_PATTERN.h`, `C_PATTERN.h`, etc. This is exactly what
`_bootstrap.c` does from the Python side. We do it once by hand (or write a
small serializer), then `#include` the result in `SNOBOL4c.c`.

Result: `SNOBOL4c.c` reads stdin, matches against `*snoExpr`, and the full
17-level SNOBOL4 expression grammar executes with zero new C code.

### The λ bridge (Sprint 6)

`Beautiful.sno` builds a pretty-print tree using `Shift`/`Reduce`/`Pop` and
`nPush`/`nInc`/`nPop` actions. Replace those Reduce actions with `λ` nodes
that emit IR instead of building a tree. The parse tree shape is known from
`Beautiful.sno`: `snoStmt` has 7 children (label, subject, pattern, `=`,
replacement, goto1, goto2). That IS the IR shape for Stage D.

---

## The SNOBOL4tiny Language — Formal Definition (Stage B)

This section defines the language that SNOBOL4-tiny compiles at Stage B
(Sprints 8–13). It is distinct from full SNOBOL4 (Stage C) and from the
primitive pattern engine (Stage A).

### What a SNOBOL4tiny Program Is

A SNOBOL4tiny program consists of exactly three things:

1. **A set of named pattern definitions** — each binding a name to a
   pattern expression (which may reference other named patterns via
   deferred `*NAME` refs).

2. **A set of action nodes** embedded within those patterns — immediate
   (`$ VAR`) and conditional (`. VAR`) — that fire side effects on match.

3. **One entry point** — `MAIN`, or the last-defined pattern if `MAIN` is
   absent.

The program reads from **stdin only**. It writes to **stdout only**. There are
no files, no environment variables, no arguments. The compiled binary is a
pipeline stage: `something | snobol4tiny_program | something_else`.

```
DIGITS  = SPAN('0123456789')
WORD    = SPAN('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ')
TOKEN   = DIGITS | WORD
MAIN    = POS(0) ARBNO(TOKEN $ OUTPUT) RPOS(0)
```

### Goal-Directed Evaluation

Every pattern node is a **generator** in the Icon sense:

- On first entry (α), it tries its first solution.
- If downstream fails and backtracks (β), it tries its next solution.
- When no more solutions exist, it propagates failure (ω) upward.
- When it finds a solution, it passes control (γ) downstream.

This means SNOBOL4tiny has full goal-directed evaluation — not just
greedy matching. `ARBNO(TOKEN $ OUTPUT)` will backtrack if downstream
fails, undoing captures if necessary.

### The Immediate vs Conditional Distinction

`$ VAR` (immediate assign) fires every time its left sub-pattern succeeds
during the course of matching — it may fire multiple times if downstream
backtracks and re-enters. It is part of the pattern execution, not deferred.

`. VAR` (conditional assign) fires at most once, after the entire top-level
match has committed. If the match fails after a conditional assign was "seen",
the assign does not fire.

When `VAR == OUTPUT`, the captured span is written to stdout immediately
(for `$`) or after commit (for `.`).

### Turing Completeness

SNOBOL4tiny at Stage B is Turing-complete for string recognition: it can
express any context-free grammar via mutually recursive named pattern
definitions. The REF node enables cycles in the IR graph.

It is not Turing-complete for general computation (no arithmetic, no
variables beyond captures) — that is Stage C.

### Relationship to Icon

Icon's goal-directed evaluation and SNOBOL4tiny's α/β/γ/ω model are the
same abstraction at different levels:

| Icon | SNOBOL4tiny |
|------|-------------|
| Generator expression | Pattern node |
| `suspend` | γ (succeed, remain resumable) |
| `fail` | ω (no more solutions) |
| Co-expression backtrack | β (downstream consumer failed, retry) |
| `every` loop | ARBNO(P) |

The difference is that SNOBOL4tiny compiles generators to static gotos,
while Icon uses a runtime generator stack. Same semantics, zero overhead.


---

## Polyglot Stdin: Alt as a Universal Parser Dispatcher

### The Idea

`SNOBOL4c.c`'s `MATCH()` engine doesn't care what grammar it's running — it just
executes a `PATTERN *` tree against a subject string. The root pattern doesn't
have to be a single grammar. It can be an **alternation** (`Π`) of multiple grammars:

```c
root = ALT(snoStmt, ALT(ednExpr, ALT(incStmt, ...)))
```

One `MATCH()` call against one stdin line. If `snoStmt` fails to match, the engine
backtracks into `ednExpr`. If that fails, it tries `incStmt`. No dispatch logic.
No format detection. No switch statement. The **backtracking engine is the dispatcher**.

This is not a special feature — it falls directly out of the Π (alternation) node
that's already implemented. Polyglot parsing is free.

### Languages Feedable Through Stdin

The initial target set:

| Language | Grammar source | Notes |
|----------|---------------|-------|
| SNOBOL4 | `snoStmt` from `Beautiful.sno` | Primary target; Sprint 5 |
| EDN | New `ednExpr` pattern | Clojure data literals; maps/vectors/keywords |
| INC | `incStmt` | `.inc` include/macro files |
| S-expressions | Subset of EDN | Trivial once EDN is done |
| CSV (single line) | `csvRow` | `BREAK(',') $ field` × N |
| Forth source | `forthWord` | Whitespace-delimited tokens |

**Why EDN specifically:** EDN (Extensible Data Notation) is the format used by
SNOBOL4-jvm (Clojure) for its internal IR serialization. A SNOBOL4-tiny that reads
EDN can directly consume SNOBOL4-jvm IR output — closing a cross-implementation loop.

### Implementation Path

Sprint 5 establishes `snoStmt` as the root grammar (from `Beautiful.sno`). Adding a
second grammar arm is a one-line change to the root `PATTERN`:

```c
// Before (Sprint 5):
root = snoStmt;

// After (EDN support):
root = ALT(snoStmt, ednExpr);
```

The `ednExpr` pattern is itself a SNOBOL4 pattern — it would live in
`src/patterns/EDN_PATTERN.h`, generated the same way as `SNOBOL4_EXPRESSION_PATTERN.h`.

### The Bootstrap Angle

Once SNOBOL4-tiny can read EDN, and SNOBOL4-jvm emits EDN IR, the pipeline becomes:

```
SNOBOL4 source
    → SNOBOL4-jvm (parses, emits EDN IR)
    → SNOBOL4-tiny stdin (reads EDN, emits C-with-gotos)
    → compiled binary
```

SNOBOL4-tiny becomes a backend for SNOBOL4-jvm output. The two implementations
validate each other through a shared IR format.

### The Alt Pattern IS the Architecture

The deeper point: the polyglot dispatcher is not a bolt-on feature. It reveals that
the SNOBOL4 pattern match IS a universal parsing framework. Any context-free language
with a SNOBOL4 grammar can be parsed by `MATCH()`. The "language" SNOBOL4-tiny
implements is therefore not SNOBOL4 — it is **whatever grammar is loaded into
`SNOBOL4_EXPRESSION_PATTERN.h`** at compile time.

This means SNOBOL4-tiny is not a SNOBOL4 compiler. It is a **compiler compiler**
whose input language is a SNOBOL4 pattern expression.

---

## Datatype Model and L-value Strategy (Session 47, 2026-03-12)

### SnoVal type tags

```c
SNO_NULL    = 0   /* empty string — DATATYPE = "STRING" */
SNO_STR     = 1   /* char*        — DATATYPE = "STRING" */
SNO_INT     = 2   /* int64_t      — DATATYPE = "INTEGER" */
SNO_REAL    = 3   /* double       — DATATYPE = "REAL" */
SNO_TREE    = 4   /* Tree*        — DATATYPE = v.t->tag  ← the kludge */
SNO_PATTERN = 5   /* Pattern*     — DATATYPE = "PATTERN" */
SNO_ARRAY   = 6   /* SnoArray*    — DATATYPE = "ARRAY" */
SNO_TABLE   = 7   /* SnoTable*    — DATATYPE = "TABLE" */
SNO_CODE    = 8   /* stub         — DATATYPE = "CODE" (never executed) */
SNO_UDEF    = 9   /* user-defined — DATATYPE = v.u->type->name */
SNO_FAIL    = 10  /* failure sentinel — not a SNOBOL4 type */
```

### The EXPRESSION kludge

SNOBOL4's EXPRESSION type is a first-class unevaluated expression object.
We represent it as `SNO_TREE` with the `.tag` field set to `"EXPRESSION"`.
`sno_datatype()` returns `v.t->tag` for SNO_TREE, so `DATATYPE(t) == "EXPRESSION"`
is true exactly when the tree was built by the expression parser.

This works because beauty.sno's EXPRESSION objects ARE parse trees —
no semantic gap between the kludge and the real semantics.

### No NAME type — l-values resolved statically

SNOBOL4 NAME type: a first-class l-value object (variable ref, array subscript,
conditional-assign target). We have none. snoc resolves all l-values at
**compile time** and emits the appropriate C call:

| L-value form | Emitted C | Mechanism |
|---|---|---|
| `X = rhs` | `sno_set(_X, rhs); sno_var_set("X", _X)` | direct, both C local + hash |
| `A[i,j] = rhs` | `sno_aset(_A, keys, 2, rhs)` | subscript direct |
| `$X = rhs` | `sno_iset(X_val, rhs)` | string→var-name indirection |
| `pat . var` | `SPAT_COND` stores `"var"` string | resolved at match time |

This is safe for beauty.sno because all l-values are syntactically visible
at compile time. NAME as a runtime first-class type is not needed for Milestone 0.

### CODE type: stub only

`CODE()` dynamically compiles a SNOBOL4 string to executable code.
beauty.sno and all its `-INCLUDE` files make **zero calls** to `CODE()`.
`SNO_CODE` exists in the type enum but is never instantiated or executed.
Not needed for Milestone 0.
