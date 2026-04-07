# IR_LOWER_SCRIP.md — Phase 5 audit: Scrip frontend lower-to-IR

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Milestone: M-G5-LOWER-SCRIP-AUDIT*

## Executive summary

**Scrip is not a language with its own IR.** It is a polyglot dispatcher:
a fenced-block source file is split by `scrip_split.py` into per-language
files (`.sno`, `.icn`, `.pro`), each compiled by its own existing frontend.
Scrip adds no new node kinds to the IR pool.

---

## Method

Read `demo/scrip/scrip_split.py`, `SESSION-scrip-jvm.md`, `SCRIP_DEMOS.md`,
`ARCH-scrip-abi.md`, and `ARCH-scrip-vision.md`.

---

## Current pipeline

```
INPUT.scrip  (Markdown with fenced blocks)
    │
    ├─ scrip_split.py  →  snobol4.sno  →  scrip-cc -jvm  →  DemoSnobol4.j
    │                  →  icon.icn     →  icon_driver -jvm →  DemoIcon.j
    │                  →  prolog.pro   →  scrip-cc -jvm -pl → DemoProlog.j
    │
    └─ inject_linkage.py  (patches cross-language static method calls)
         │
         └─ jasmin / java  →  running program
```

Each block is independently lowered by its language's existing frontend
and emitter. `scrip_split.py` is a ~60-line Python script. There is no
`scrip_parse()`, no Scrip AST, no Scrip IR.

---

## IR node kinds contributed by Scrip

**None.** Every node in a Scrip program originates from one of the five
language frontends. The unified IR pool already covers all of them:

| Language block | IR pool used |
|---|---|
| SNOBOL4 block | SNOBOL4 pool (E_QLIT, E_ILIT, E_ADD, E_CONCAT, E_ARB, etc.) |
| Icon block | Icon pool (ICN_INT, ICN_STR, ICN_TO, ICN_EVERY, ICN_SCAN, etc.) |
| Prolog block | Prolog pool (E_CLAUSE, E_UNIFY, E_CUT, E_CHOICE, etc.) |
| Snocone block | SNOBOL4 pool (shared 90%+) |
| Rebus block | SNOBOL4 + Icon pools (50/50) |

---

## Gap table

| # | Gap | Severity | Notes |
|---|---|---|---|
| G1 | No formal `scrip_parse()` / dispatcher in `main.c` | Medium | Currently demo-only Python script. Production path needs a C splitter integrated into the build. |
| G2 | Cross-language call linkage (`inject_linkage.py`) is post-compile patching | Medium | Correct for demos; for production needs ABI-level design (ARCH-scrip-abi.md, FROZEN). |
| G3 | Scrip blocks currently limited to SNOBOL4 + Icon + Prolog (JVM only) | Low | x64 and .NET Scrip demos not yet wired. Not an IR gap — a harness gap. |

---

## Conclusion

**M-G5-LOWER-SCRIP-AUDIT: PASS — no IR gaps.**

Scrip contributes zero new node kinds. The IR pool is already sufficient.
All gaps are harness/integration concerns, not IR lowering concerns.
M-G5-LOWER-SCRIP-FIX is a no-op at the IR level.

The production Scrip compiler (Level 2 of the Scrip vision) will require
a C block-splitter in `main.c` and a real cross-language linker per
`ARCH-scrip-abi.md`, but those are post-M-G7-UNFREEZE scope and do not
touch the IR enum.

---
*IR_LOWER_SCRIP.md — authored G-9 s14. Do not add content beyond this line.*
