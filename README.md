# snobol4x

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

A SNOBOL4/SPITBOL compiler with five frontends and four backends — x86-64 native ASM,
JVM bytecode, .NET MSIL, and portable C — all from a single IR.
Part of the [snobol4ever](https://github.com/snobol4ever) organization.

---

## What This Is

`snobol4x` (the **TINY** compiler) is a from-scratch SNOBOL4 compiler: one frontend
pipeline (`sno2c`) feeding four independent backend emitters. Write SNOBOL4 once.
Run it anywhere.

| Flag | Output | Status |
|------|--------|--------|
| *(default)* | Portable C with labeled gotos | ✅ 106/106 corpus |
| `-asm` | x86-64 NASM assembly | ✅ 97/106 corpus · 9 known failures |
| `-jvm` | JVM Jasmin bytecode (`.j`) | ✅ 106/106 corpus · `beauty.sno` ✅ |
| `-net` | .NET CIL assembly (`.il`) | ✅ 110/110 corpus · roman + wordcount ✅ |

The 9 ASM failures (tests 022, 055, 064, cross, word1–4, wordcount) are under active
investigation via the five-way differential monitor.

Sister repos: [`snobol4jvm`](https://github.com/snobol4ever/snobol4jvm) (full Clojure→JVM
pipeline, 2,033 tests) and [`snobol4dotnet`](https://github.com/snobol4ever/snobol4dotnet)
(full C#→MSIL pipeline, 1,874 tests).

---

## The Architecture — Byrd Boxes All the Way Down

Every SNOBOL4 statement has the same shape:

```
label:   subject   pattern   = replacement   :S(goto)  :F(goto)
```

Each pattern node compiles to a **Byrd box** — four labeled entry points wired at
compile time, zero runtime dispatch:

| Port | Greek | Meaning |
|------|-------|---------|
| **proceed** | **α** | Enter fresh — cursor at current position |
| **recede**  | **β** | Resume after backtrack from a child |
| **succeed** | **γ** | Match succeeded — advance cursor, pass forward |
| **concede** | **ω** | Match failed — restore cursor, propagate back |

Sequential composition wires γ of one node to α of the next. Alternation saves the
cursor on the left-ω path and restores it before trying right. ARBNO wires child-γ
back into its own α until child-ω exits. The wiring *is* the execution — no interpreter
table, no virtual dispatch on the hot path.

This model, first described by Lawrence Byrd in 1980 for Prolog debugging and
generalized by Todd Proebsting in 1996 as a syntax-directed code generation strategy
for goal-directed languages, turns out to describe SNOBOL4 pattern matching exactly.
All four backends implement the same four-port wiring. The semantics are identical
whether the output is C labeled gotos, x86-64 JMP instructions, JVM `goto` bytecodes,
or CIL `br` instructions.

**Hot path:** pure labeled gotos. Zero overhead. No `setjmp` on the hot path.
**Cold path:** `longjmp` for `ABORT`, bare `FENCE`, and genuine runtime errors only.

---

## Five Frontends

Five frontends share the same IR (`EXPR_t` / `STMT_t`):

| Frontend | Source language | Status |
|----------|----------------|--------|
| **SNOBOL4 / SPITBOL** | Full SNOBOL4 + SPITBOL extensions | ✅ active — all backends |
| **Snocone** | Andrew Koenig's structured C-like frontend (Bell Labs TR 124, 1986) | ✅ active — ASM backend (`-sc -asm`) |
| **Rebus** | Structured transpiler — Rebus source → SNOBOL4 | ✅ complete — M-REBUS ✅ |
| **Tiny-Icon** | Icon generators via Byrd Box IR | 🗓 planned — post-bootstrap |
| **Tiny-Prolog** | Prolog unification via Byrd Box IR | 🗓 planned — post-bootstrap |

The Byrd Box IR is the bridge between languages. Icon generators map to the same
four ports. Prolog unification is goal-directed evaluation — the same model.
SNOBOL4, Icon, and Prolog are three syntaxes for one execution machine.

---

## Build

```bash
# Dependencies
apt-get install -y libgc-dev nasm default-jdk

# Build sno2c
make -C src

# C backend (default)
./sno2c program.sno > prog.c && gcc prog.c -lgc -o prog && ./prog

# ASM backend
./sno2c -asm program.sno > prog.s
nasm -f elf64 prog.s -o prog.o && gcc prog.o -lgc -o prog && ./prog

# JVM backend
./sno2c -jvm program.sno > prog.j
java -jar src/backend/jvm/jasmin.jar prog.j -d .
java -cp . Prog

# NET backend
./sno2c -net program.sno > prog.il
ilasm prog.il && mono prog.exe
```

---

## Corpus Ladder

All backends climb the same 12-rung ladder against
[`snobol4corpus`](https://github.com/snobol4ever/snobol4corpus)/`crosscheck/`:

```
Rung  1: hello / output    Rung  5: control       Rung  9: keywords
Rung  2: assign            Rung  6: patterns       Rung 10: functions
Rung  3: concat            Rung  7: capture        Rung 11: data
Rung  4: arith             Rung  8: strings        Rung 12: beauty.sno
```

| Backend | Corpus | Rung 12 | Notes |
|---------|:------:|:-------:|-------|
| C (portable) | ✅ 106/106 | — | Full corpus |
| x86-64 ASM | ⚠ 97/106 | — | 9 known failures; monitor investigation active |
| JVM bytecode | ✅ 106/106 | ✅ | `beauty.sno` self-beautifies — M-JVM-BEAUTY ✅ |
| .NET MSIL | ✅ 110/110 | — | roman + wordcount pass — M-NET-SAMPLES ✅ |

**Oracle:** CSNOBOL4 2.3.3 — `snobol4 -f -P256k -I$INC file.sno`

---

## Validate

```bash
# C backend
bash test/crosscheck/run_crosscheck.sh

# ASM backend (STOP_ON_FAIL=0 shows all results)
STOP_ON_FAIL=0 bash test/crosscheck/run_crosscheck_asm_corpus.sh

# JVM backend
JASMIN=src/backend/jvm/jasmin.jar
bash test/crosscheck/run_crosscheck_jvm.sh

# NET backend
bash test/crosscheck/run_crosscheck_net.sh
```

---

## Correctness — Chomsky Hierarchy Oracles

SNOBOL4 patterns are not a regex engine. They are a universal grammar machine.
The corpus includes mathematical oracles at every tier of the Chomsky hierarchy:

| Tier | Oracle language | All backends |
|------|----------------|:------------:|
| Type 3 — Regular | `(a\|b)*abb`, `a*b*`, `{x^2n}` | ✅ |
| Type 2 — Context-free | `{a^n b^n}`, palindromes, Dyck language | ✅ |
| Type 1 — Context-sensitive | `{a^n b^n c^n}` | ✅ |
| Type 0 — Turing | `{w#w}` copy language | ✅ |

These are proven results, not empirical approximations. A backend either computes the
correct answer or it does not.

---

## Repository Layout

```
src/
  frontend/
    snobol4/          SNOBOL4/SPITBOL lexer + parser → AST + IR
    snocone/          Snocone frontend (SC language, ~10 source files)
    rebus/            Rebus transpiler
    icon/             Tiny-Icon (planned)
    prolog/           Tiny-Prolog (planned)
  backend/
    c/                Portable C emitter (emit_byrd.c 2,709 lines · emit.c 2,220 lines)
    x64/              x86-64 NASM emitter (emit_byrd_asm.c 4,159 lines)
    jvm/              JVM Jasmin emitter (emit_byrd_jvm.c 4,051 lines · jasmin.jar)
    net/              .NET CIL emitter (emit_byrd_net.c 1,934 lines)
  driver/
    main.c            sno2c entry point — flag dispatch
  runtime/
    asm/              NASM macro library + runtime helpers
test/
  crosscheck/         106-program corpus + .ref oracle outputs
  sprint_asm/         ASM regression suite
  rebus/              Rebus round-trip tests
  smoke/              Quick sanity tests
artifacts/
  asm/
    beauty_prog.s     beauty.sno → x86-64 ASM (tracked canonical output)
    samples/
      roman.s         roman.sno → x86-64 ASM
      wordcount.s     wordcount.sno → x86-64 ASM
  jvm/                hello_prog.j · roman.j · wordcount.j
  net/                hello_prog.il
  c/                  Canonical C outputs
```

---

## The Five-Way Monitor

Active on the `asm-backend` branch: a parallel differential monitor that runs the
same SNOBOL4 program through all five participants simultaneously and compares trace
streams event-by-event via named FIFOs.

| # | Participant | Role |
|---|-------------|------|
| 1 | CSNOBOL4 2.3.3 | Primary oracle |
| 2 | SPITBOL x64 4.0f | Secondary oracle |
| 3 | snobol4x ASM backend | Compiled target |
| 4 | snobol4x JVM backend | Compiled target |
| 5 | snobol4x NET backend | Compiled target |

`monitor_ipc.so` — a LOAD'd C shared library — writes trace events to a per-participant
named FIFO, bypassing stdio entirely. The collector reads all five FIFOs in parallel.
The first line where any participant diverges from the oracle is the exact statement,
variable, and value where the bug fires. No bisecting. No guessing.

**Status (2026-03-21):** CSNOBOL4 ✅ · SPITBOL ✅ · ASM ✅ working in isolation.
JVM OUTPUT fast-path hook and NET emitter hook in progress — M-MONITOR-IPC-5WAY next.

---

## The Bootstrap Goal

The correctness target is self-hosting. Two gates:

**M-BEAUTIFY-BOOTSTRAP** — `beauty.sno` (the SNOBOL4 beautifier written in SNOBOL4)
reads itself and produces output identical to its input on all backends. A fixed point.

**M-COMPILER-BOOTSTRAP** — `compiler.sno` (the full compiler written in SNOBOL4)
compiles itself.

The JVM backend has already passed Rung 12: `beauty.sno` via the JVM backend produces
output byte-for-byte identical to the CSNOBOL4 oracle (M-JVM-BEAUTY ✅, commit
`b67d0b1` J-212). The other backends follow.

---

## The Development Story

snobol4x is co-authored by **Lon Jones Cherryholmes** and **Claude Sonnet 4.6**.

The sessions run like a buddy comedy: Lon arrives with an architectural insight or an
inconvenient bug, Claude writes the code, they argue about the right abstraction, one
of them is wrong, they figure out which one, the milestone fires, and Claude writes the
commit. Then they do it again, starting fresh with no memory of the previous session
except whatever made it into the docs.

The architecture has a name for that: the session log. Every session's mental state
at handoff is recorded in
[SESSIONS_ARCHIVE.md](https://github.com/snobol4ever/.github/blob/main/SESSIONS_ARCHIVE.md)
so the next Claude can pick up exactly where the last one left off. It is, in a way,
the compiler writing itself — one session at a time.

---

## Active Development

Sprint state lives in [snobol4ever/.github](https://github.com/snobol4ever/.github):

- **PLAN.md** — milestone dashboard, 4D feature matrix
- **TINY.md** — snobol4x sprint state, invariants, next actions per session
- **MONITOR.md** — five-way monitor design and sprint detail
- **SESSIONS_ARCHIVE.md** — full session history, append-only

**Current sprint:** `monitor-ipc` — completing M-MONITOR-IPC-5WAY, then the
19-subsystem `beauty.sno` bootstrap sequence.

---

## Collaborators

- **Lon Jones Cherryholmes** — compiler architecture, all backends, snobol4x lead
- **Jeffrey Cooper, M.D.** — snobol4dotnet, .NET MSIL target
- **Claude Sonnet 4.6** — TINY co-author; every sprint, every Byrd box,
  every labeled goto — written in session, committed, pushed
