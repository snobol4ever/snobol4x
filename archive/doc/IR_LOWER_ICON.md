# IR_LOWER_ICON.md — Phase 5 audit: Icon frontend lower-to-IR

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Milestone: M-G5-LOWER-ICON-AUDIT*

## Method

Enumerated every `ICN_*` kind in `src/frontend/icon/icon_ast.h` (the complete
`IcnKind` enum, 64 values including `ICN_KIND_COUNT`).  Cross-referenced
against the `emit_expr` dispatch tables in both backends:

- **x64:** `src/backend/x64/emit_x64_icon.c` — the `switch(n->kind)` in `emit_expr`
- **JVM:** `src/backend/jvm/emit_jvm_icon.c` — the `switch(n->kind)` in `ij_emit_expr`

A kind is **implemented** if there is a non-stub `case` for it (i.e. not a
`default:` fallthrough that emits `; UNIMPL` or jumps unconditionally to ω).
A kind is **stub** if the case exists but deliberately emits fail/0/noop with
a comment noting deferred list/record support.
A kind is **missing** if there is no case at all in the dispatch table.

## Complete kind table

| ICN kind | x64 | JVM | Notes |
|---|---|---|---|
| ICN_INT | ✅ | ✅ | |
| ICN_REAL | ✅ | ✅ | x64: truncates to long (documented) |
| ICN_STR | ✅ | ✅ | |
| ICN_CSET | ✅ | ✅ | treated as typed string |
| ICN_VAR | ✅ | ✅ | |
| ICN_ADD | ✅ | ✅ | |
| ICN_SUB | ✅ | ✅ | |
| ICN_MUL | ✅ | ✅ | |
| ICN_DIV | ✅ | ✅ | |
| ICN_MOD | ✅ | ✅ | |
| ICN_POW | ✅ | ✅ | |
| ICN_NEG | ✅ | ✅ | |
| ICN_POS | ⚠️ GAP | ⚠️ GAP | `+E` unary — no case in either backend; falls to `default` (UNIMPL/fail) |
| ICN_RANDOM | ⚠️ GAP | ⚠️ GAP | `?E` — no case; falls to default |
| ICN_COMPLEMENT | ⚠️ GAP | ⚠️ GAP | `~E` cset complement — no case |
| ICN_CSET_UNION | ⚠️ GAP | ⚠️ GAP | `E1 ++ E2` — no case |
| ICN_CSET_DIFF | ⚠️ GAP | ⚠️ GAP | `E1 -- E2` — no case |
| ICN_CSET_INTER | ⚠️ GAP | ⚠️ GAP | `E1 ** E2` — no case |
| ICN_BANG_BINARY | stub | stub | `E1 ! E2` — emits fail; list runtime deferred |
| ICN_SECTION_PLUS | ✅ | ✅ | delegates to emit_section |
| ICN_SECTION_MINUS | ✅ | ✅ | delegates to emit_section |
| ICN_LT | ✅ | ✅ | |
| ICN_LE | ✅ | ✅ | |
| ICN_GT | ✅ | ✅ | |
| ICN_GE | ✅ | ✅ | |
| ICN_EQ | ✅ | ✅ | |
| ICN_NE | ✅ | ✅ | |
| ICN_SLT | ✅ | ✅ | |
| ICN_SLE | ✅ | ✅ | |
| ICN_SGT | ✅ | ✅ | |
| ICN_SGE | ✅ | ✅ | |
| ICN_SEQ | ✅ | ✅ | string equality |
| ICN_SNE | ✅ | ✅ | |
| ICN_CONCAT | ✅ | ✅ | `\|\|` |
| ICN_LCONCAT | ✅ | ✅ | `\|\|\|` — both backends alias to concat |
| ICN_TO | ✅ | ✅ | |
| ICN_TO_BY | ✅ | ✅ | |
| ICN_ALT | ✅ | ✅ | n-ary |
| ICN_AND | ✅ | ✅ | n-ary conjunction |
| ICN_BANG | ✅ | ✅ | string-element generator; list stub |
| ICN_SIZE | ✅ | ✅ | `*E` |
| ICN_LIMIT | ✅ | ✅ | `E \ N` |
| ICN_NOT | ✅ | ✅ | |
| ICN_NONNULL | ✅ | ✅ | |
| ICN_NULL | ✅ | ✅ | |
| ICN_SEQ_EXPR | ✅ | ✅ | `E1 ; E2` |
| ICN_EVERY | ✅ | ✅ | |
| ICN_WHILE | ✅ | ✅ | |
| ICN_UNTIL | ✅ | ✅ | |
| ICN_REPEAT | ✅ | ✅ | |
| ICN_IF | ✅ | ✅ | |
| ICN_CASE | ✅ | ✅ | |
| ICN_ASSIGN | ✅ | ✅ | |
| ICN_AUGOP | ✅ | ✅ | all subtypes via synthetic node |
| ICN_SWAP | ✅ | ✅ | |
| ICN_IDENTICAL | ✅ | ✅ | |
| ICN_MATCH | ✅ | ✅ | `=E` scan pattern |
| ICN_SCAN | ✅ | ✅ | `E ? body` |
| ICN_SCAN_AUGOP | ⚠️ GAP | ⚠️ GAP | `E ?:= body` — no case in either backend |
| ICN_CALL | ✅ | ✅ | builtins + user procs |
| ICN_RETURN | ✅ | ✅ | |
| ICN_SUSPEND | ✅ | ✅ | |
| ICN_FAIL | ✅ | ✅ | |
| ICN_BREAK | ✅ | ✅ | |
| ICN_NEXT | ✅ | ✅ | |
| ICN_PROC | ✅ | ✅ | outer procedure wrapper |
| ICN_FIELD | stub | ✅ | x64: push 0 stub; JVM: has ij_emit_field |
| ICN_SUBSCRIPT | ✅ | ✅ | |
| ICN_SECTION | ✅ | ✅ | |
| ICN_MAKELIST | stub | ✅ | x64: push 0 stub; JVM: has ij_emit_makelist |
| ICN_RECORD | stub | ✅ | x64: push 0 stub; JVM: handles record decls |
| ICN_GLOBAL | ✅ | ✅ | local decl — skipped in stmt chain |
| ICN_INITIAL | ✅ | ✅ | runs-once guard via BSS flag |

## Gap table

| # | ICN kind | Both backends | Priority | Notes |
|---|---|---|---|---|
| G1 | `ICN_POS` | both missing | Low | `+E` is a no-op for integers (identity); for strings, converts to numeric. In practice rarely generated — parser may fold it. Fix: forward to child, push value unchanged (x64) / same (JVM). |
| G2 | `ICN_RANDOM` | both missing | Medium | `?E` — random integer 1..E (integer arg) or random char from cset/string. Needs `icn_random(n)` runtime call (x64) / `IcnRuntime.random(long)` (JVM). Parser generates this for `?expr`. |
| G3 | `ICN_COMPLEMENT` | both missing | Medium | `~E` — cset complement. Needs `icn_cset_complement(cset)` runtime call. |
| G4 | `ICN_CSET_UNION` | both missing | Medium | `E1 ++ E2` — cset union. Needs `icn_cset_union(a, b)` runtime call. |
| G5 | `ICN_CSET_DIFF` | both missing | Medium | `E1 -- E2` — cset difference. Needs `icn_cset_diff(a, b)` runtime call. |
| G6 | `ICN_CSET_INTER` | both missing | Medium | `E1 ** E2` — cset intersection. Needs `icn_cset_inter(a, b)` runtime call. |
| G7 | `ICN_SCAN_AUGOP` | both missing | Low | `E ?:= body` — augmented scan assignment. Rarely used; can stub-fail for now. |

### Stub-only (not true gaps — deferred by design)

| ICN kind | x64 | JVM | Decision |
|---|---|---|---|
| `ICN_BANG_BINARY` | stub-fail | stub-call | List runtime not in x64; JVM aliases to ij_emit_call. Intentional. |
| `ICN_MAKELIST` | push-0 stub | ✅ implemented | x64 list runtime deferred. Fix tracked in x64-specific backlog. |
| `ICN_RECORD` | push-0 stub | ✅ implemented | x64 record runtime deferred. |
| `ICN_FIELD` | push-0 stub | ✅ implemented | x64 field access deferred. |

## Corpus impact

The rung corpus (`programs/icon/rung01–rung38`) does not exercise cset
operators (G3–G6) or `ICN_RANDOM`/`ICN_POS` heavily — the existing 38-rung
invariant passes with these gaps.  The gaps affect programs that use cset
arithmetic directly (e.g. `~&letters`, `cs1 ++ cs2`).

## Conclusion

**M-G5-LOWER-ICON-AUDIT: COMPLETE — 7 gaps identified.**

Gaps G1 and G7 are low priority (rare in corpus, easy to stub-fail).
Gaps G2–G6 are medium priority: cset operations and `?E` random are
part of standard Icon semantics and will be needed before the rung count
can grow beyond rung 38.

Fix milestones needed:

| Milestone | Scope |
|---|---|
| M-G5-LOWER-ICON-FIX-POS | G1: ICN_POS — both backends |
| M-G5-LOWER-ICON-FIX-RANDOM | G2: ICN_RANDOM — both backends + runtime |
| M-G5-LOWER-ICON-FIX-CSET | G3–G6: ICN_COMPLEMENT, ICN_CSET_UNION/DIFF/INTER — both backends + runtime |
| M-G5-LOWER-ICON-FIX-SCAN-AUGOP | G7: ICN_SCAN_AUGOP — both backends (stub-fail acceptable) |

These fix milestones are **post-reorg scope** (after M-G7-UNFREEZE) unless
a gap directly blocks a currently-failing invariant rung.

---
*IR_LOWER_ICON.md — authored G-9 s14. Do not add content beyond this line.*
