# Artifacts

One canonical file per artifact. Git history is the archive — no numbered session copies.
Overwrite and commit when the artifact changes. `git log -p artifacts/asm/beauty_prog.s` shows full evolution.

## Ownership — never cross these boundaries

| Directory | Owned by | Session may NOT write |
|-----------|----------|-----------------------|
| `artifacts/asm/` | ASM backend session (B-) | JVM session, NET session, SD session |
| `artifacts/jvm/` | JVM backend session (J-) | ASM session, NET session |
| `artifacts/jvm/samples/` | JVM session (J-) **and** Scrip Demo session (SD-) | ASM session, NET session |
| `artifacts/net/` | NET backend session (N-) | ASM session, JVM session |
| `artifacts/icon/` | Icon JVM session (IJ-) **and** SD session | all others |
| `artifacts/prolog/` | Prolog JVM session (PJ-) **and** SD session | all others |

Toolchain verification (e.g. "does jasmin.jar work?") goes to `/tmp/` — never to another backend's artifact directory.

```
artifacts/
  asm/
    beauty_prog.s        ← PRIMARY: beauty.sno compiled via -asm
    fixtures/            ← sprint oracle .s files (one per node type / milestone)
    samples/             ← sample programs (roman, wordcount)
  c/                     ← C backend generated output (.c files)
  jvm/
    hello_prog.j         ← null/hello program via -jvm (canonical J0+ artifact)
    hello.j              ← hello.sno via -jvm
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
INC=/home/claude/corpus/programs/inc
BEAUTY=/home/claude/corpus/programs/beauty/beauty.sno
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

---

## Session 195 — JVM backend (2026-03-19)

| Artifact | md5 | lines | status |
|----------|-----|-------|--------|
| artifacts/jvm/hello.j | 204412d41015788277c30a6ecbcd7e8f | 31 | assembles clean |
| artifacts/jvm/multi.j | 1a904881f5152e4ab42ac7fb51d622d9 | 37 | assembles clean |
| artifacts/jvm/literals.j | c9419f5bd4ca01a00acfcf1683e73ec7 | 362 | assembles clean |

Sprint: J1 complete → M-JVM-LIT ✅. Active bug: none. Next: J2 (assign/ + arith/).

---

## icon/samples/ — Icon frontend artifacts

| File | What | Status |
|------|------|--------|
| hello.icn + hello.j | Hello World | ✅ ICON-JVM PASS |
| wordcount.icn + wordcount.j | Word count | ✅ ICON-JVM PASS |
| roman.icn + roman.j | Roman numerals (SCRIP demo idiom) | ✅ ICON-JVM PASS |
| palindrome.icn + palindrome.j | Palindrome test | ✅ ICON-JVM PASS |
| generators.icn | range/divisors/fibs/perfect generators | ✅ icont PASS · IJ-JVM pending rung36 fix |
| queens.icn | N-Queens backtracking | ✅ icont PASS · IJ-JVM pending rung36 fix |

**Regenerate .j files after IJ rung36 VerifyError fix:**
```bash
for icn in artifacts/icon/samples/*.icn; do
    base="${icn%.icn}"
    /tmp/sno2c -jvm "$icn" -o "${base}.j" 2>/dev/null
done
```

---

## prolog/samples/ — Prolog frontend artifacts

| File | What | Status |
|------|------|--------|
| hello.pro + hello.j | Hello World | ✅ SWIPL + PROLOG-JVM PASS |
| wordcount.pro + wordcount.j | Word count | ✅ SWIPL + PROLOG-JVM PASS |
| roman.pro + roman.j | Roman numerals | ✅ SWIPL + PROLOG-JVM PASS |
| palindrome.pro + palindrome.j | Palindrome test | ✅ SWIPL + PROLOG-JVM PASS |
| queens.pro | N-Queens via backtracking | ✅ SWIPL PASS · PJ-JVM pending forall/2 fix |
| sentences.pro | DCG sentence parser + generator | ✅ SWIPL PASS · PJ-JVM pending DCG fix |

**Regenerate .j files after PJ fixes:**
```bash
for pro in artifacts/prolog/samples/*.pro; do
    base="${pro%.pro}"
    ./sno2c -pl -jvm "$pro" > "${base}.j" 2>/dev/null
done
```

---

## Regen commands — run after touching any emitter

```bash
export JAVA_TOOL_OPTIONS=""
cd /home/claude/snobol4x
JASMIN=src/backend/jvm/jasmin.jar
INC=demo/inc
mkdir -p /tmp/art_out

# --- SNOBOL4 × JVM (touch emit_byrd_jvm.c) ---
./sno2c -jvm -I$INC demo/roman.sno    > artifacts/jvm/samples/roman.j
./sno2c -jvm -I$INC demo/wordcount.sno > artifacts/jvm/samples/wordcount.j
printf "        OUTPUT = 'HELLO WORLD'\nEND\n" | ./sno2c -jvm -I$INC /dev/stdin > artifacts/jvm/hello.j

# --- SNOBOL4 × ASM (touch emit_byrd_asm.c) ---
./sno2c -asm -I$INC demo/roman.sno     > artifacts/asm/samples/roman.s
./sno2c -asm -I$INC demo/wordcount.sno > artifacts/asm/samples/wordcount.s
./sno2c -asm -I$INC demo/beauty.sno    > artifacts/asm/beauty_prog.s

# --- SNOBOL4 × NET (touch emit_byrd_net.c) ---
./sno2c -net -I$INC demo/roman.sno     > artifacts/net/samples/roman.il
./sno2c -net -I$INC demo/wordcount.sno > artifacts/net/samples/wordcount.il

# --- Icon × JVM (touch icon_emit_jvm.c) ---
# Build sno2c first: gcc ... -o /tmp/sno2c (see SESSION-scrip-jvm.md §BUILD)
for icn in artifacts/icon/samples/*.icn; do
    base="${icn%.icn}"
    /tmp/sno2c -jvm "$icn" -o "${base}.j" 2>/dev/null
    java -jar $JASMIN "${base}.j" -d /tmp/art_out/ 2>&1 | grep -i error | grep -v Picked && echo "FAIL ${base}.j" || echo "OK   ${base}.j"
done

# --- Prolog × JVM (touch prolog_emit_jvm.c) ---
for pro in artifacts/prolog/samples/*.pro; do
    base="${pro%.pro}"
    ./sno2c -pl -jvm "$pro" > "${base}.j" 2>/dev/null
    java -jar $JASMIN "${base}.j" -d /tmp/art_out/ 2>&1 | grep -i error | grep -v Picked && echo "FAIL ${base}.j" || echo "OK   ${base}.j"
done

# --- Verify all .j files assemble ---
for j in artifacts/**/*.j; do
    java -jar $JASMIN "$j" -d /tmp/art_out/ 2>&1 | grep -i error | grep -v Picked && echo "BROKEN: $j" || true
done
```
