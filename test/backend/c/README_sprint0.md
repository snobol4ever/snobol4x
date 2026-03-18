# Sprint 0 — Null Program

The null program: a SNOBOL4 program with no statements.
Expected output: nothing. Exit status: 0.

## Files

- `null.sno` — SNOBOL4 source (reference)
- `null.c` — hand-compiled C-with-gotos (Sprint 0 target)
- `null_expected.txt` — expected output (empty)

## Build and Test

```bash
cc -o null null.c ../../src/runtime/runtime.c && ./null > got.txt
diff null_expected.txt got.txt && echo "PASS" || echo "FAIL"
```

## What This Establishes

- Full alpha/beta/gamma/omega skeleton compiles and links
- Runtime (runtime.h / runtime.c) builds cleanly
- Exit 0 on success, exit 1 on failure
