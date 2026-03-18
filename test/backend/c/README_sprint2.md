# Sprint 2 — Concatenation (CAT)

Two-node and three-node sequences. Tests that P_γ→Q_α and Q_ω→P_β are wired
correctly by emit_c.py.

## Test Cases

| File | Pattern | Subject | Expected |
|------|---------|---------|----------|
| `cat_pos_lit_rpos.c` | `POS(0) "hello" RPOS(0)` | `"hello"` | `hello` |

## Hand-Written Reference

`cat_pos_lit_rpos.c` is the gold standard for what emit_c.py must produce.
Compile and run it before touching the emitter.

## Build and Test

```bash
# Hand-written reference
cc -o cat_ref cat_pos_lit_rpos.c ../../src/runtime/runtime.c
./cat_ref > got.txt
diff cat_pos_lit_rpos_expected.txt got.txt && echo "PASS: hand-written"

# Emitter-generated
cd ../..
python3 - << 'EOF'
import sys
sys.path.insert(0, 'src/ir')
sys.path.insert(0, 'src/codegen')
from ir import Graph, Cat, Pos, Rpos, Lit, Assign
from emit_c import emit_program
g = Graph()
g.add("root", Assign(Cat(Pos(0), Cat(Lit("hello"), Rpos(0))), "OUTPUT"))
print(emit_program(g, "root", subject="hello"))
EOF > /tmp/sprint2_gen.c
cc -I src/runtime -o /tmp/sprint2_gen /tmp/sprint2_gen.c src/runtime/runtime.c
/tmp/sprint2_gen > /tmp/sprint2_got.txt
diff test/sprint2/cat_pos_lit_rpos_expected.txt /tmp/sprint2_got.txt && echo "PASS: emitter"
```

## What This Establishes

- CAT node wires P_γ → Q_α and Q_ω → P_β correctly
- Assign node records start cursor, runs child, captures span on success
- emit_c.py produces a binary that matches the hand-written reference output
