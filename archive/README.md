# archive/frontend + archive/backend — Python prototype era

Archived session182. These files have no build role in the C compiler.

## frontend/

| File | What it was |
|------|-------------|
| `parser.py` | Early Python SNOBOL4 parser prototype, predates the C lex/yacc frontend. Uses `ir.py` Graph nodes. |
| `sno_parser.py` | More complete Python parser (Sprint 16+), handles full Stmt/Expr/PatExpr IR. Referenced `ir.py` Expr, PatExpr. ~1000 lines. |

Live SNOBOL4 frontend is in `src/frontend/snobol4/` (lex.c, parse.c, sno.l, sno.y, sno2c.h).
Live Rebus frontend is in `src/frontend/rebus/` (own Makefile, M-REBUS ✅).

Removed from `src/frontend/`: `icon/`, `snocone/.gitkeep`, `prolog/.gitkeep`.

## backend/

| File | What it was |
|------|-------------|
| `emit_jvm.py` | Python JVM bytecode emitter. Imports `byrd_ir` + `lower`. Smoke test at bottom generates chunks for a test pattern. Never connected to a live build. |
| `emit_msil.py` | Python .NET MSIL emitter. Same architecture as emit_jvm.py. Proof-of-concept only. |
| `emit_c.py` | Python C emitter using `ir.py` Graph nodes. Earlier generation than the C `emit.c`. |
| `emit_c_byrd.py` | Python C emitter using `byrd_ir` four-port model. Bridge generation between ir.py and C backend. |
| `emit_c_stmt.py` | Python C statement emitter (~1375 lines). Full Stmt/Expr/PatExpr → C. Prototype of what became `emit.c` + `emit_byrd.c`. |

Live backends are in `src/backend/c/` and `src/backend/x64/` (C only).
JVM and MSIL backends planned in C — Python prototypes are reference only.

## Why kept (not deleted)

`emit_jvm.py` and `emit_msil.py` are the closest thing to a JVM/MSIL design
spec. When those backends are eventually built in C/Clojure/C#, these files
answer "what does the emitter need to do" at the chunk level.

`emit_c_stmt.py` is a full working Python prototype of the statement emitter —
useful as a correctness reference during C backend development.
