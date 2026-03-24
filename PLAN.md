# snobol4x — Sprint Plan

## §START — Bootstrap (ALWAYS FIRST)

```bash
cd /home/claude/snobol4ever
bash snobol4x/setup.sh          # must end: 106/106 ALL PASS
ln -sfn /home/claude/snobol4ever/x64 /home/claude/x64   # if x64 missing
```

---

## Current milestone: `M-BEAUTY-SEMANTIC` (B-275)

1. Check `demo/inc/XDump.sno` exists; if not, create driver + ref from CSNOBOL4 oracle
2. `INC=demo/inc bash test/beauty/run_beauty_subsystem.sh XDump`
3. Fix any ASM divergences
4. On PASS: `git commit -m "B-275: M-BEAUTY-XDUMP ✅"` + push
5. Update `.github/PLAN.md` TINY backend row, advance to `M-BEAUTY-SEMANTIC`

**Fixed B-274:** `expr_flatten_str` in `emit_byrd_asm.c` — multi-line DEFINE specs
(E_CONC of E_QLITs) now registered as user functions; `fn_Read_γ/ω` emitted;
FRETURN inside `Read` body routes correctly. 8/8 ASM PASS. Commit `eeeb5ad`.

---

## Beauty subsystem sequence

| #  | Subsystem   | Status |
|----|-------------|--------|
| 1  | global      | ✅ |
| 2  | is          | ✅ |
| 3  | FENCE       | ✅ |
| 4  | io          | ✅ |
| 5  | case        | ✅ |
| 6  | assign      | ✅ |
| 7  | match       | ✅ |
| 8  | counter     | ✅ |
| 9  | stack       | ✅ |
| 10 | tree        | ✅ |
| 11 | ShiftReduce | ✅ |
| 12 | TDump       | ✅ |
| 13 | Gen         | ✅ |
| 14 | Qize        | ✅ |
| 15 | ReadWrite   | ✅ |
| 16 | XDump       | ✅ |
| 17 | semantic    | ← now |

| 18 | omega       | |

---

## Trigger phrases
- `"playing with beauty"` → B-session → beauty milestone above
- `"playing with Prolog frontend"` → F-session → M-PROLOG-R10 (bi→ucall_seq label fix)

## L2 docs
- `doc/DESIGN.md` — architecture / emitter
- `doc/BOOTSTRAP.md` — env setup
- `doc/DECISIONS.md` — decisions log
- `.github/BEAUTY.md` — beauty subsystem plan
- `.github/SESSIONS_ARCHIVE.md` — session history
- `.github/PLAN.md` — HQ dashboard
