# Artifacts

One canonical file per artifact. Git history is the archive — no numbered session copies.
Overwrite and commit when the artifact changes. `git log -p artifacts/asm/beauty_prog.s` shows full evolution.

## Ownership — never cross these boundaries

| Directory | Owned by | Session may NOT write |
|-----------|----------|-----------------------|
| `artifacts/asm/` | ASM backend session | JVM session, NET session |
| `artifacts/jvm/` | JVM backend session | ASM session, NET session |
| `artifacts/net/` | NET backend session | ASM session, JVM session |
| `artifacts/c/`   | C backend session   | all others |

Toolchain verification (e.g. "does jasmin.jar work?") goes to `/tmp/` — never to another backend's artifact directory.

```
artifacts/
  asm/
    beauty_prog.s        ← PRIMARY: beauty.sno compiled via -asm
    fixtures/            ← sprint oracle .s files (one per node type / milestone)
    samples/             ← sample programs (roman, wordcount)
  c/                     ← C backend generated output (.c files)
  jvm/
    hello.j              ← hello.sno via -jvm  (canonical J1+ artifact)
    multi.j              ← multi.sno via -jvm
    literals.j           ← literals.sno via -jvm
  net/
    hello_prog.il        ← hello/null program via -net (canonical N0+ artifact)
```

---

## asm/beauty_prog.s — PRIMARY ARTIFACT

The canonical output of `sno2c -asm` on `beauty.sno`. This is what M-ASM-BEAUTIFUL was declared on.
Every session that changes the ASM backend must regenerate and commit this file.

**Update:**
```bash
INC=/home/claude/snobol4corpus/programs/inc
BEAUTY=/home/claude/snobol4corpus/programs/beauty/beauty.sno
src/sno2c/sno2c -asm -I$INC $BEAUTY > artifacts/asm/beauty_prog.s
nasm -f elf64 -I src/runtime/asm/ artifacts/asm/beauty_prog.s -o /dev/null  # must be clean
git add artifacts/asm/beauty_prog.s && git commit
```

---

## asm/fixtures/ — Sprint oracle fixtures

One file per node type or milestone. Hand-written or generated during sprint work.
These are reference/regression files — not updated every session.

| File | Node/feature | Milestone |
|------|-------------|-----------|
| null.s | empty program | M-ASM-HELLO ✅ |
| lit_hello.s | LIT node | M-ASM-LIT ✅ |
| pos0_rpos0.s, cat_pos_lit_rpos.s | POS/RPOS/SEQ | M-ASM-SEQ ✅ |
| alt_first.s, alt_second.s, alt_fail.s | ALT | M-ASM-ALT ✅ |
| arbno_match.s, arbno_empty.s, arbno_alt.s | ARBNO | M-ASM-ARBNO ✅ |
| any_vowel.s, notany_consonant.s, span_digits.s, break_space.s | charset | M-ASM-CHARSET ✅ |
| assign_lit.s, assign_digits.s | $ capture | M-ASM-ASSIGN ✅ |
| ref_astar_bstar.s, anbn.s | named patterns | M-ASM-NAMED ✅ |
| multi_capture_abc.s, star_deref_capture.s | multi-capture, *VAR | M-ASM-CROSSCHECK ✅ |
| stmt_output_lit.s, stmt_goto.s | program-mode statements | M-ASM-BEAUTY work |

---

## c/beauty_full.c — C backend historical reference

`beauty_full.c` is the last known-good `-trampoline` C output from the Bug5/6 era (sessions 103–116).
It differs from `beauty_prog.c` in that it was compiled during active debugging and reflects
the state of `emit_byrd.c` at session114. Kept for historical reference; `beauty_prog.c` is the canonical current artifact.

---

## asm/samples/ — Sample programs

| File | What | Status |
|------|------|--------|
| roman.s | roman numeral converter | assembles; output placeholder |
| wordcount.s | word count program | assembles with warning |
