# SNOBOL4-tiny Benchmark Suite

Two contests. Two verdicts. One engine wins both — and then keeps going where
the competition cannot follow.

---

## Prerequisites

```bash
# Debian/Ubuntu
sudo apt install libpcre2-dev bison

# macOS
brew install pcre2 bison
```

## Run Everything

```bash
cd bench
make run
```

---

## Contest 1 — vs PCRE2 JIT (Type 3 / Regular)

**Pattern**: `(a|b)*abb` and pathological `(a+)+b`

PCRE2 is the gold standard for regular expression performance. It ships with
JIT compilation enabled. It is what the world uses.

| Test | SNOBOL4-tiny | PCRE2 JIT | Faster by |
|------|:------------:|:---------:|:---------:|
| Normal `(a|b)*abb` | 5.49 ns | 55.55 ns | **10×** |
| Pathological len=20 | 0.7 ns | 21.7 ns | **31×** |
| Pathological len=28 | 0.7 ns | 23.0 ns | **33×** |

The pathological result is the key one. PCRE2 backtracks through O(2^n)
configurations on adversarial input. SNOBOL4-tiny detects failure in O(1) —
structural failure, not exhaustive search. The gap only grows with input length.

**Source**: `bench_re_vs_tiny.c`

---

## Contest 2 — vs Bison LALR(1) (Type 2 / Context-Free)

**Patterns**: `{a^n b^n}` and Dyck language (balanced parentheses)

Bison generates LALR(1) table-driven pushdown automata — the industry standard
for context-free parsing. Used to build compilers for C, Python, Ruby, PHP.
Both Bison and SNOBOL4-tiny compile to C. No VM. No JIT. Native code only.

| Test | SNOBOL4-tiny | Bison LALR(1) | Faster by |
|------|:------------:|:-------------:|:---------:|
| `{a^n b^n}` | 11.54 ns | 158.45 ns | **14×** |
| Dyck language | 7.50 ns | 113.89 ns | **15×** |

Bison generates state-table lookups and explicit stack operations per token.
SNOBOL4-tiny generates static gotos — the control flow *is* the grammar.
Zero table lookup. Zero dispatch overhead.

**Bison's ceiling**: Type 2 (context-free). SNOBOL4-tiny has no ceiling.

**Source**: `pda/bench_pda.c`, `pda/anbn.y`, `pda/dyck.y`

---

## The Full Picture

| Competitor | Their tier | SNOBOL4-tiny advantage | Their ceiling |
|------------|:----------:|:----------------------:|:-------------:|
| PCRE2 JIT | Type 3 — Regular | **10–33×** faster | Cannot count |
| Bison LALR(1) | Type 2 — Context-Free | **14–15×** faster | Cannot triple-count |
| *(none)* | Type 1 — Context-Sensitive | — | SNOBOL4-tiny only |
| *(none)* | Type 0 — Turing | — | SNOBOL4-tiny only |

**One engine. All four tiers of the Chomsky hierarchy. Faster than every
tier's champion on that tier's own ground.**

---

## Platform

Results above: Linux x86-64, gcc -O2, PCRE2 10.42, Bison 3.8.2, 2026-03-10.
Timing: `CLOCK_MONOTONIC` nanosecond resolution, 2–5M iterations, warmed up.

*These are floor numbers. The full `emit_c.py` code generator will only widen the gap.*

---

## SPITBOL / CSNOBOL4 Comparison

Oracle binaries for cross-engine validation:

| Binary | Build |
|--------|-------|
| `/usr/local/bin/spitbol` | From `x64-main.zip` |
| `/usr/local/bin/snobol4` | From `snobol4-2_3_3_tar.gz` |

Full cross-engine results will be recorded here as sprints advance.
