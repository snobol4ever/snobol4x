# Artifacts

One canonical file per artifact. Git history is the archive — no numbered session copies.
Overwrite and commit when the artifact changes. `git log -p artifacts/asm/beauty_prog.s` shows full evolution.

```
artifacts/
  asm/    — x64 NASM output (.s files)
  c/      — C backend generated output (.c files)
  jvm/    — JVM bytecode and Clojure output (future)
  net/    — .NET MSIL output (future)
```

---

## asm/ — x64 NASM

### beauty_prog.s
- **what:** beauty.sno compiled via `-asm` — primary sprint oracle
- **update:** `src/sno2c/sno2c -asm -I$INC $BEAUTY > artifacts/asm/beauty_prog.s`
- **verify:** `nasm -f elf64 -I src/runtime/asm/ artifacts/asm/beauty_prog.s -o /dev/null`

### Sprint oracle fixtures (one file per node type / milestone)
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
| stmt_output_lit.s, stmt_assign.s, stmt_goto.s | program-mode statements | M-ASM-BEAUTY work |

---

## c/ — C backend

### beauty_prog.c
- **what:** beauty.sno compiled via `-trampoline` — C backend oracle
- **update:** `src/sno2c/sno2c -trampoline -I$INC $BEAUTY > artifacts/c/beauty_prog.c`
- **last known state:** session116, 16307 lines

### Trampoline fixtures
| File | What |
|------|------|
| trampoline_hello.c | hello world via trampoline backend |
| trampoline_branch.c | branching via trampoline backend |
| trampoline_fn.c | function calls via trampoline backend |

---

## jvm/ — JVM bytecode *(future)*

Clojure→bytecode output. Placeholder — populated when JVM backend produces artifacts.

---

## net/ — .NET MSIL *(future)*

C#→MSIL output. Placeholder — populated when DOTNET backend produces artifacts.

---

## Protocol

- **Never create** `foo_sessionN.ext` — overwrite `foo.ext` and commit
- `git log -p artifacts/TYPE/foo.ext` — full history
- Commit message: `sessionN: artifacts — foo.ext updated (reason)`
