# demo/ — Five canonical SNOBOL4 demo programs

All source, includes, and data in one place. These five programs are the
canonical tracked artifacts for all three backends (ASM, JVM, NET).

## Programs

| Program | Description | Input | Data file |
|---------|-------------|-------|-----------|
| `roman.sno` | Roman numeral converter (recursive) | numbers on stdin | — |
| `wordcount.sno` | Count words in input | text on stdin | `wordcount.input` |
| `beauty.sno` | SNOBOL4 source beautifier | `.sno` source on stdin | includes from `inc/` |
| `treebank.sno` | Penn Treebank S-expr parser | S-exprs on stdin | `treebank.ref` (oracle) |
| `claws5.sno` | CLAWS5 POS corpus tokenizer | CLAWS5-tagged text on stdin | `CLAWS5inTASA.dat` |

## Layout

```
demo/
  roman.sno           — recursive Roman numeral conversion
  wordcount.sno       — word count
  beauty.sno          — SNOBOL4 beautifier (main)
  expression.sno      — expression parser (included by beauty.sno)
  treebank.sno        — Penn Treebank S-expr parser
  claws5.sno          — CLAWS5 POS tokenizer (inlines stack primitives)
  wordcount.input     — sample input for wordcount
  treebank.ref        — oracle output for treebank (CSNOBOL4)
  CLAWS5inTASA.dat    — TASA corpus input for claws5
  inc/                — shared SNOBOL4 include library
    global.sno
    is.sno
    FENCE.sno
    io.sno
    case.sno
    stack.sno
```

## Running via CSNOBOL4 oracle

```bash
INC=demo/inc

# roman — pipe numbers in
echo "12" | snobol4 -f -I$INC demo/roman.sno

# wordcount
snobol4 -f -I$INC demo/wordcount.sno < demo/wordcount.input

# beauty (self-beautify)
snobol4 -f -I$INC demo/beauty.sno < demo/beauty.sno

# treebank
snobol4 -f -I$INC demo/treebank.sno < demo/treebank.ref

# claws5
snobol4 -f -I$INC demo/claws5.sno < demo/CLAWS5inTASA.dat
```

## Artifact status (ASM backend)

| Artifact | Path | Status |
|----------|------|--------|
| `beauty_prog.s` | `artifacts/asm/beauty_prog.s` | ✅ assembles clean |
| `roman.s` | `artifacts/asm/samples/roman.s` | ✅ assembles clean |
| `wordcount.s` | `artifacts/asm/samples/wordcount.s` | ✅ assembles clean |
| `treebank.s` | `artifacts/asm/samples/treebank.s` | ✅ assembles clean |
| `claws5.s` | `artifacts/asm/samples/claws5.s` | ⚠️ ~95% (3 undef β labels — NRETURN) |

See RULES.md §ASM ARTIFACTS for the regeneration script.
