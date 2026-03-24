# snobol4x — Sprint Plan

## §START — Bootstrap (ALWAYS FIRST)

```bash
cd /home/claude/snobol4ever
bash snobol4x/setup.sh          # must end: 106/106 ALL PASS
ln -sfn /home/claude/snobol4ever/x64 /home/claude/x64   # if x64 missing
```

---

## Current milestone: `M-BEAUTY-READWRITE` (B-274)

1. Check/fix `_b_OUTPUT` 3-arg form in `src/runtime/snobol4/snobol4.c` (same n==3 as `_b_INPUT`)
2. `cd src && make`
3. `INC=demo/inc bash test/beauty/run_beauty_subsystem.sh ReadWrite`
4. On 8/8 PASS: `git commit -m "B-274: M-BEAUTY-READWRITE ✅"` + push
5. Update `.github/PLAN.md` TINY backend row, advance to `M-BEAUTY-XDUMP`

**Fixed this session:** `_b_INPUT` n==3 (bad path returned NULVCL not FAILDESCR).
Steps 1–5 pass; `_b_OUTPUT` n==3 fix needed for steps 6–8.

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
| 15 | ReadWrite   | ← now |
| 16 | XDump       | |
| 17 | semantic    | |
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
