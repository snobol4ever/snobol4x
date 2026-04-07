# IR_AUDIT.md — Pre-Reorg Frontend IR Audit (revised G-7)

**Produced by:** G-7 session, 2026-03-28
**Milestone:** M-G0-IR-AUDIT
**Purpose:** Map every construct in all six frontends to the minimal unified
`EKind` set. The test for a new node kind: does it require distinct Byrd box
α/β/γ/ω wiring that cannot be expressed as a composition of existing nodes?
If it can be lowered or is a builtin call, it does not get its own EKind.

---

## Result: 44 Node Kinds — Starting Point, Not Frozen

The unified IR has **44 node kinds**. This is the minimal set covering all six
frontends. The previous audit draft overcounted by ~35 nodes due to mechanical
1:1 mapping of source AST kinds rather than principled lowering analysis.

See GRAND_MASTER_REORG.md § The Shared IR for the canonical table with α/β
wiring. This document explains the lowering decisions.
**This 44-node set is analysis-time best judgment, not a frozen contract.**
The real test comes during Phases 3–5 when emitters are actually unified.
Some lowering combinations that look clean on paper may require distinct β-wiring
in practice — those warrant new nodes. Some nodes kept separate may prove to emit
identically across all backends — those merge. The CISC/RISC tension between
giving each construct its own node vs pushing complexity into the lowering layer
is resolved empirically by the code, not theoretically in advance.
See GRAND_MASTER_REORG.md § IR Node Set — Living Target for the change protocol.

---

## The Lowering Principle

Most source-level constructs do NOT need their own EKind. They lower to
compositions of existing nodes or to E_FNC builtin calls. A new EKind is
justified only when the Byrd box wiring is structurally distinct — when
α and β behave in a way no existing node provides.

---

## Frontend Coverage

### SNOBOL4 — gap: 0

All SNOBOL4 constructs are already covered. The existing scrip-cc.h EKind enum
is the basis of the shared IR, extended by new entries for Icon/Rebus.

### Prolog — gap: 0

Uses exactly 6 EKind nodes, all present:
E_CLAUSE, E_CHOICE, E_UNIFY, E_CUT, E_TRAIL_MARK, E_TRAIL_UNWIND.

### Snocone — gap: 0

snocone_lower.c maps every operator to existing SNOBOL4 EKind nodes.
No new kinds needed.

### Scrip — gap: 0

Polyglot container. Dispatches to five frontends. Cross-language calls
are E_FNC. No new node kinds.

### Icon — gap: 2 new nodes

60 IcnKind entries lower to the shared IR as follows:

**Literals:**
  ICN_INT -> E_ILIT
  ICN_REAL -> E_FLIT
  ICN_STR -> E_QLIT
  ICN_CSET -> E_CSET  (NEW — distinct type; pattern engine dispatches differently)
  ICN_VAR -> E_VAR

**Arithmetic:**
  ICN_ADD/SUB/MUL/DIV/MOD/POW/NEG -> E_ADD/SUB/MPY/DIV/MOD/POW/MNS
  ICN_POS (unary +) -> identity, lowers to child

**Comparisons — all E_FNC:**
  ICN_LT/LE/GT/GE/EQ/NE -> E_FNC("LT",2) etc.
  ICN_SLT/SLE/SGT/SGE/SEQ/SNE -> E_FNC("SLT",2) etc.
  Goal-directed but the Byrd box wiring is E_FNC — builtin handles succeed/fail.

**String/cset ops — all E_FNC:**
  ICN_CONCAT (||) -> E_SEQ  (string concatenation = sequencing)
  ICN_LCONCAT (|||) -> E_FNC("lconcat",2)
  ICN_COMPLEMENT, ICN_CSET_UNION/DIFF/INTER -> E_FNC
  ICN_SIZE (*E) -> E_FNC("size",1)
  ICN_RANDOM (?E) -> E_FNC("random",1)

**Control flow — key insight: all lower to E_SEQ/E_ALT/E_ARBNO compositions:**
  ICN_SEQ_EXPR (;) -> E_SEQ  (sequencing IS concatenation)
  ICN_AND (&) -> E_SEQ  (conjunction IS sequencing)
  ICN_IF then A else B -> E_ALT(E_SEQ(cond,A), B)
  ICN_EVERY E do body -> E_ARBNO(E_SEQ(E, body))
  ICN_WHILE E do body -> E_ARBNO(E_SEQ(E, body))
  ICN_UNTIL E do body -> E_ARBNO(E_SEQ(E_ALT(fail_node,E), body))
  ICN_REPEAT body -> E_ARBNO(body)
  All loop/control forms share the same α/β wiring as E_ARBNO+E_SEQ.

**Assignment:**
  ICN_ASSIGN -> E_ASSIGN
  ICN_AUGOP (:= family) -> E_ASSIGN(lhs, E_op(lhs, rhs))  — composition
  ICN_SWAP -> E_SWAP

**Access:**
  ICN_SUBSCRIPT (E[i]) -> E_IDX
  ICN_FIELD (E.name) -> E_IDX(E, E_QLIT("name"))  — named subscript
  ICN_SECTION (E[i:j]) -> E_FNC("sub",3)
  ICN_SECTION_PLUS/MINUS -> E_FNC

**Logical:**
  ICN_NOT -> E_ALT(E_SEQ(cond, E_CUT), fail)  — FENCE pattern
  ICN_NONNULL (\E), ICN_NULL (/E) -> E_FNC
  ICN_IDENTICAL (===) -> E_FNC

**Generators — all exist:**
  ICN_TO -> E_TO
  ICN_TO_BY -> E_TO_BY
  ICN_ALT -> E_GENALT
  ICN_BANG -> E_ITER
  ICN_LIMIT -> E_LIMIT
  ICN_SCAN -> E_MATCH
  ICN_SCAN_AUGOP -> E_ASSIGN + E_MATCH
  ICN_MATCH (=E) -> E_MATCH + E_FNC("match")

**Call/suspend:**
  ICN_CALL -> E_FNC
  ICN_SUSPEND -> E_SUSPEND

**Control transfer — NOT EKind (γ/ω routing at stmt level):**
  ICN_RETURN, ICN_FAIL, ICN_BREAK, ICN_NEXT

**Declarations — NOT EKind (Program/STMT_t level):**
  ICN_PROC, ICN_RECORD, ICN_GLOBAL, ICN_INITIAL

**List constructor — genuinely new:**
  ICN_MAKELIST ([e1,e2,...]) -> E_MAKELIST  (NEW — n-ary, unique α semantics:
  evaluate all children in order, build list object. No existing node covers this.)

**Icon gap: 2 new EKind entries: E_CSET, E_MAKELIST.**

### Rebus — gap: 0 additional

Rebus is a SNOBOL4/Icon hybrid. Every REKind and RSKind maps to an existing
SNOBOL4 EKind, the two new Icon kinds, or a lowering/E_FNC call.

  P-component (SNOBOL4 patterns): RE_COND/IMM/CURSOR/DEREF -> E_CAPT_COND/DOL/ATP/STAR
  L-component (Icon control): same lowering as Icon
  RE_UNLESS -> FENCE pattern via E_CUT
  RE_FOR -> E_SEQ(E_ASSIGN, E_ARBNO(...)) composition
  RE_PATOPT (~pat) -> E_ALT(pat, E_QLIT(""))  — alternation with empty. Collapses.
  All arithmetic/comparison/assignment -> existing kinds or E_FNC

Rebus additional gap: 0.

---

## Name Alias Issue — must resolve in M-G1

The reorg doc uses E_CAPT_COND/E_CAPT_IMM for cursor/value capture.
scrip-cc.h uses E_CAPT_COND/E_CAPT_IMM for the same nodes.
The reorg doc uses E_ASSIGN; scrip-cc.h uses E_ASSIGN.

M-G1-IR-HEADER-DEF picks the canonical names for ir.h.
Recommendation: use reorg doc names (E_CAPT_COND, E_CAPT_IMM, E_ASSIGN) — more
self-documenting. scrip-cc.h gets #define aliases during Phase 1, removed in Phase 3.

---

## New Entries for ir.h Summary

Beyond what currently exists in scrip-cc.h, ir.h adds:

From the planned shared IR table (not yet in scrip-cc.h):
  E_ARB, E_ARBNO, E_POS, E_RPOS, E_CAPT_COND, E_CAPT_IMM, E_ASSIGN (canonical name),
  E_SUSPEND, E_TO, E_TO_BY, E_LIMIT, E_GENALT, E_ITER, E_MATCH, E_SWAP,
  E_POW, E_MOD

From this audit (genuinely new):
  E_CSET, E_MAKELIST

Total ir.h: 44 node kinds.

---

*M-G0-IR-AUDIT complete (revised). 44-node set is the target for ir.h.*
*Next: M-G1-IR-HEADER-DEF.*

---

## Addendum — Pattern Primitives (G-7 final pass)

The initial audit incorrectly collapsed all SNOBOL4 pattern primitives to
`E_FNC`. This was wrong. Each has distinct Byrd box wiring in `emit_byrd_asm.c`
(separate `emit_xxx` functions) and a distinct `p$xxx` match routine in SPITBOL
MINIMAL. They all require their own `EKind`.

Source of truth: `emit_byrd_asm.c` lines 2420-2422 — the recognized builtin list:

| New node | Operation | SPITBOL | Wiring distinction |
|----------|-----------|---------|-------------------|
| `E_ANY` | `ANY(S)` match one char from cset | `p$any` | cursor+1 on match |
| `E_NOTANY` | `NOTANY(S)` match char not in S | — | cursor+1 on match |
| `E_SPAN` | `SPAN(S)` longest run from S | `p$spn` | cursor+N on match |
| `E_BREAK` | `BREAK(S)` up to char in S | `p$brk` | stops before delimiter |
| `E_BREAKX` | `BREAKX(S)` break with backtrack | `p$bkx` | β advances past delimiter |
| `E_LEN` | `LEN(N)` exactly N chars | `p$len` | fixed advance |
| `E_TAB` | `TAB(N)` to position N | `p$tab` | absolute cursor set |
| `E_RTAB` | `RTAB(N)` to N from right | `p$rtb` | right-anchored |
| `E_REM` | `REM` remainder | `p$rem` | always matches rest |
| `E_FAIL` | `FAIL` always fail | `p$fal` | α and β both → ω |
| `E_SUCCEED` | `SUCCEED` always succeed | `p$suc` | α and β both → γ |
| `E_FENCE` | `FENCE` seal β | `XFNCE=35` | β → abort |
| `E_ABORT` | `ABORT` abort match | — | propagates abort |
| `E_BAL` | `BAL` balanced parens | `p$bal` | recursive balance check |

**Total IR nodes: 59** (45 + 14 pattern primitives).

Icon equivalents (`upto()`, `move()`, `tab()`, `match()` in scanning context)
map to these same nodes during M-G5-LOWER-ICON.
