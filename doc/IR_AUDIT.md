# IR_AUDIT.md — Pre-Reorg Frontend IR Audit (revised G-7)

**Produced by:** G-7 session, 2026-03-28
**Milestone:** M-G0-IR-AUDIT
**Purpose:** Map every construct in all six frontends to the minimal unified
`EKind` set. The test for a new node kind: does it require distinct Byrd box
α/β/γ/ω wiring that cannot be expressed as a composition of existing nodes?
If it can be lowered or is a builtin call, it does not get its own EKind.

---

## Result: 37 Node Kinds

The unified IR has **37 node kinds**. This is the minimal set covering all six
frontends. The previous audit draft overcounted by ~35 nodes due to mechanical
1:1 mapping of source AST kinds rather than principled lowering analysis.

See GRAND_MASTER_REORG.md § The Shared IR for the canonical table with α/β
wiring. This document explains the lowering decisions.

---

## The Lowering Principle

Most source-level constructs do NOT need their own EKind. They lower to
compositions of existing nodes or to E_FNC builtin calls. A new EKind is
justified only when the Byrd box wiring is structurally distinct — when
α and β behave in a way no existing node provides.

---

## Frontend Coverage

### SNOBOL4 — gap: 0

All SNOBOL4 constructs are already covered. The existing sno2c.h EKind enum
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
  ICN_VAR -> E_VART

**Arithmetic:**
  ICN_ADD/SUB/MUL/DIV/MOD/POW/NEG -> E_ADD/SUB/MPY/DIV/MOD/POW/MNS
  ICN_POS (unary +) -> identity, lowers to child

**Comparisons — all E_FNC:**
  ICN_LT/LE/GT/GE/EQ/NE -> E_FNC("LT",2) etc.
  ICN_SLT/SLE/SGT/SGE/SEQ/SNE -> E_FNC("SLT",2) etc.
  Goal-directed but the Byrd box wiring is E_FNC — builtin handles succeed/fail.

**String/cset ops — all E_FNC:**
  ICN_CONCAT (||) -> E_CONC  (string concatenation = sequencing)
  ICN_LCONCAT (|||) -> E_FNC("lconcat",2)
  ICN_COMPLEMENT, ICN_CSET_UNION/DIFF/INTER -> E_FNC
  ICN_SIZE (*E) -> E_FNC("size",1)
  ICN_RANDOM (?E) -> E_FNC("random",1)

**Control flow — key insight: all lower to E_CONC/E_OR/E_ARBNO compositions:**
  ICN_SEQ_EXPR (;) -> E_CONC  (sequencing IS concatenation)
  ICN_AND (&) -> E_CONC  (conjunction IS sequencing)
  ICN_IF then A else B -> E_OR(E_CONC(cond,A), B)
  ICN_EVERY E do body -> E_ARBNO(E_CONC(E, body))
  ICN_WHILE E do body -> E_ARBNO(E_CONC(E, body))
  ICN_UNTIL E do body -> E_ARBNO(E_CONC(E_OR(fail_node,E), body))
  ICN_REPEAT body -> E_ARBNO(body)
  All loop/control forms share the same α/β wiring as E_ARBNO+E_CONC.

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
  ICN_NOT -> E_OR(E_CONC(cond, E_CUT), fail)  — FENCE pattern
  ICN_NONNULL (\E), ICN_NULL (/E) -> E_FNC
  ICN_IDENTICAL (===) -> E_FNC

**Generators — all exist:**
  ICN_TO -> E_TO
  ICN_TO_BY -> E_TO_BY
  ICN_ALT -> E_ALT_GEN
  ICN_BANG -> E_BANG
  ICN_LIMIT -> E_LIMIT
  ICN_SCAN -> E_SCAN
  ICN_SCAN_AUGOP -> E_ASSIGN + E_SCAN
  ICN_MATCH (=E) -> E_SCAN + E_FNC("match")

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

  P-component (SNOBOL4 patterns): RE_COND/IMM/CURSOR/DEREF -> E_NAM/DOL/ATP/STAR
  L-component (Icon control): same lowering as Icon
  RE_UNLESS -> FENCE pattern via E_CUT
  RE_FOR -> E_CONC(E_ASSIGN, E_ARBNO(...)) composition
  RE_PATOPT (~pat) -> E_OR(pat, E_QLIT(""))  — alternation with empty. Collapses.
  All arithmetic/comparison/assignment -> existing kinds or E_FNC

Rebus additional gap: 0.

---

## Name Alias Issue — must resolve in M-G1

The reorg doc uses E_DOT/E_DOLLAR for cursor/value capture.
sno2c.h uses E_NAM/E_DOL for the same nodes.
The reorg doc uses E_ASSIGN; sno2c.h uses E_ASGN.

M-G1-IR-HEADER-DEF picks the canonical names for ir.h.
Recommendation: use reorg doc names (E_DOT, E_DOLLAR, E_ASSIGN) — more
self-documenting. sno2c.h gets #define aliases during Phase 1, removed in Phase 3.

---

## New Entries for ir.h Summary

Beyond what currently exists in sno2c.h, ir.h adds:

From the planned shared IR table (not yet in sno2c.h):
  E_ARB, E_ARBNO, E_POS, E_RPOS, E_DOT, E_DOLLAR, E_ASSIGN (canonical name),
  E_SUSPEND, E_TO, E_TO_BY, E_LIMIT, E_ALT_GEN, E_BANG, E_SCAN, E_SWAP,
  E_POW, E_MOD

From this audit (genuinely new):
  E_CSET, E_MAKELIST

Total ir.h: 37 node kinds.

---

*M-G0-IR-AUDIT complete (revised). 37-node set is the target for ir.h.*
*Next: M-G1-IR-HEADER-DEF.*
