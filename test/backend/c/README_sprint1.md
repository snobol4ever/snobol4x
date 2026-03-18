# Sprint 1 — Single Token

Single-node patterns: literal match, POS(0), RPOS(0).

## Test Cases

| File | Pattern | Subject | Expected |
|------|---------|---------|----------|
| `lit_hello.c` | `"hello"` | `"hello"` | match |
| `pos0.c` | `POS(0)` | `"abc"` | match (cursor at 0) |
| `rpos0.c` | `RPOS(0)` | `"abc"` | match only at end |

## Build and Test

```bash
for f in lit_hello pos0 rpos0; do
  cc -o $f $f.c ../../src/runtime/runtime.c
  ./$ f > got.txt
  diff ${f}_expected.txt got.txt && echo "PASS: $f" || echo "FAIL: $f"
done
```
