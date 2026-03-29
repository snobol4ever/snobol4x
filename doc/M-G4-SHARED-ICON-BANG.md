# M-G4-SHARED-ICON-BANG — Audit: E_ITER (ICN_BANG) / E_MATCH (ICN_MATCH)

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — x64 stubs; nothing to align

---

## ICN_BANG

### x64 (`emit_bang`, ~line 2201)

**Status: stub.** `emit_bang` immediately jumps both α and β to `ports.ω` (fail).
Comment: "Stub: just fail — list/string iteration needs runtime support."
`ICN_BANG_BINARY` is also a stub (`emit_stub_fail`).

### JVM (`ij_emit_bang`, ~line 5395)

**Status: fully implemented.** Two dispatch branches:

**List branch** (`ij_expr_is_list(child)`):
- Static fields: `icn_N_bang_list` (ArrayList ref), `icn_N_bang_idx` (int counter).
- α: eval child → store list, reset idx=0.
- β: resume from `chk` (list already stored, just re-check).
- check: `list.size() <= idx` → ω; else `list.get(idx++)` → γ.
- Supports both scalar Long lists and record (Object) lists.

**String branch** (default):
- Static fields: `icn_N_bang_str` (String ref), `icn_N_bang_pos` (int cursor).
- α: eval child → store string, reset pos=0.
- β: resume from `check` (string already stored, re-check).
- check: `pos >= length` → ω; else `substring(pos, pos+1)` → γ; `pos++`.

---

## ICN_MATCH

### x64

**Status: stub.** `emit_stub_fail` — both α and β jump to `ports.ω`.

### JVM (`ij_emit_match`, ~line 6453)

**Status: fully implemented.**
- Evaluates pattern expression E, stores in static `icn_N_match_pat` field.
- Calls `IjRT.icn_rt_match(subject, pos, pat) → long` (new pos or -1).
- On success: updates `IjRT.icn_pos`, yields matched substring → γ.
- One-shot (no β retry — β → ω immediately).

---

## Divergence summary

x64 has no implementation for either node kind. There is nothing to extract —
the extraction milestone presupposes two implementations to unify. The JVM
implementations are the reference specs for when x64 implements these.

| Node | x64 | JVM |
|------|-----|-----|
| ICN_BANG (string) | stub (→ ω) | full: pos cursor in static int field |
| ICN_BANG (list) | stub (→ ω) | full: idx counter + ArrayList static field |
| ICN_MATCH | stub (→ ω) | full: IjRT.icn_rt_match() + pos update |

## Future work (out of reorg scope)

x64 ICN_BANG needs:
1. Runtime: `icn_bang_str_init(char*)` + `icn_bang_str_next() → char*` (single-char
   string alloc), backed by BSS pos slot.
2. Runtime: `icn_bang_list_init(IcnList*)` + `icn_bang_list_next() → DESCR_t*`.
3. Emitter: `emit_bang` wires α → init call, β → next call, fail → ω.

x64 ICN_MATCH needs:
1. Runtime: `icn_rt_match(char *subj, int64_t pos, char *pat) → int64_t`.
2. Emitter: `emit_match` calls runtime, updates `icn_pos` BSS slot.

Tracking: these are Phase 5/6 scope (frontend lowering + new pipeline cells),
not Phase 4 (shared Byrd-box wiring). Phase 4 has nothing to do here.

## Decision

M-G4-SHARED-ICON-BANG: CLOSED (nothing to extract; x64 is stub-only).
x64 BANG/MATCH implementation tracked as a separate backlog item.

