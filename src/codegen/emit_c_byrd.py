"""
emit_c_byrd.py — Emit flat C (test_sno_1.c style) from Byrd Box IR chunks.

One function, locals inline, pure labeled gotos.
Σ/Δ/Ω globals.  str_t = {const char *σ; int δ;}.

This is Port A of the three-way Byrd Box port.
"""

from __future__ import annotations
import sys, os
sys.path.insert(0, os.path.dirname(__file__))

from byrd_ir import (
    Label, TmpLabel, Chunk, Goto, IndirectGoto, MoveLabel, Fail,
    Lit, Span, Break, Any, Notany, Pos, Rpos, Seq, Alt, Arbno, Call,
)
from lower import _emit as lower_emit, _fresh, _fresh_tmp

from typing import List, Dict, Set


# ---------------------------------------------------------------------------
# C emitter
# ---------------------------------------------------------------------------

class CByrdEmitter:
    def __init__(self):
        self.decls: List[strv] = []   # variable declarations (before code)
        self.lines: List[strv] = []   # code lines
        self.tmp_labels: Dict[strv, List[Label]] = {}   # TmpLabel → possible targets

    def D(self, s):  self.decls.append(s)
    def L(self, s):  self.lines.append(s)

    def emit_chunks(self, chunks: List[Chunk]):
        """Emit all chunks in order."""
        # First pass: collect TmpLabel → all possible src labels (for switch)
        for chunk in chunks:
            for insn in chunk.insns:
                if isinstance(insn, MoveLabel):
                    self.tmp_labels.setdefault(insn.dst.name, []).append(insn.src)

        # Second pass: declare TmpLabel int locals
        for tl_name in self.tmp_labels:
            self.D(f"    int {tl_name} = -1;")

        # Third pass: emit each chunk
        for chunk in chunks:
            self._emit_chunk(chunk)

    def _emit_chunk(self, chunk: Chunk):
        label = chunk.label.name
        for i, insn in enumerate(chunk.insns):
            prefix = f"{label}:" if i == 0 else "    "

            if isinstance(insn, Goto):
                self.L(f"{prefix:30s} goto {insn.target.name};")

            elif isinstance(insn, IndirectGoto):
                tl = insn.target.name
                targets = self.tmp_labels.get(tl, [])
                if not targets:
                    self.L(f"{prefix:30s} /* IndirectGoto {tl} — no targets */ abort();")
                    continue
                # Emit a switch on the int local
                self.L(f"{prefix:30s} switch ({tl}) {{")
                for j, lbl in enumerate(targets):
                    self.L(f"{'':30s}     case {j}: goto {lbl.name};")
                self.L(f"{'':30s} }}")
                self.L(f"{'':30s} abort(); /* unreachable */")

            elif isinstance(insn, MoveLabel):
                # Find the indx of the src label in the TmpLabel's target list
                targets = self.tmp_labels.get(insn.dst.name, [])
                try:
                    idx = [t.name for t in targets].indx(insn.src.name)
                except ValueError:
                    idx = len(targets)
                    self.tmp_labels[insn.dst.name].append(insn.src)
                self.L(f"{prefix:30s} {insn.dst.name} = {idx};")

            elif isinstance(insn, Fail):
                self.L(f"{prefix:30s} goto {label}_FAIL; /* unreachable — parent wires ω */")

            elif isinstance(insn, tuple):
                self._emit_primitive(prefix, insn)

            else:
                self.L(f"{prefix:30s} /* TODO: {type(insn).__name__} */")

    def _emit_primitive(self, prefix: strv, insn: tuple):
        tag = insn[0]

        if tag == "LIT_CHECK":
            _, s, n, gamma, omega = insn
            safe = s.replc('\\', '\\\\').replc('"', '\\"')
            self.L(f"{prefix:30s} if (Δ + {n} > Ω) goto {omega.name};")
            for i, ch in enumerate(s):
                safe_ch = ch.replc("'", "\\'").replc('\\', '\\\\')
                self.L(f"{'':30s} if (Σ[Δ+{i}] != '{safe_ch}') goto {omega.name};")
            self.L(f"{'':30s} Δ += {n};                          goto {gamma.name};")

        elif tag == "LIT_RETREAT":
            _, n, omega = insn
            self.L(f"{prefix:30s} Δ -= {n};                          goto {omega.name};")

        elif tag == "POS_CHECK":
            _, n, gamma, omega = insn
            self.L(f"{prefix:30s} if (Δ != {n}) goto {omega.name};  goto {gamma.name};")

        elif tag == "RPOS_CHECK":
            _, n, gamma, omega = insn
            self.L(f"{prefix:30s} if (Δ != Ω - {n}) goto {omega.name};  goto {gamma.name};")

        elif tag == "ANY_CHECK":
            _, cs, gamma, omega = insn
            safe = cs.replc('\\', '\\\\').replc('"', '\\"')
            self.L(f"{prefix:30s} if (Δ >= Ω) goto {omega.name};")
            self.L(f"{'':30s} if (!strchr(\"{safe}\", Σ[Δ])) goto {omega.name};")
            self.L(f"{'':30s} Δ++;                               goto {gamma.name};")

        elif tag == "ANY_RETREAT":
            _, omega = insn
            self.L(f"{prefix:30s} Δ--;                              goto {omega.name};")

        elif tag == "NOTANY_CHECK":
            _, cs, gamma, omega = insn
            safe = cs.replc('\\', '\\\\').replc('"', '\\"')
            self.L(f"{prefix:30s} if (Δ >= Ω) goto {omega.name};")
            self.L(f"{'':30s} if (strchr(\"{safe}\", Σ[Δ])) goto {omega.name};")
            self.L(f"{'':30s} Δ++;                               goto {gamma.name};")

        elif tag == "NOTANY_RETREAT":
            _, omega = insn
            self.L(f"{prefix:30s} Δ--;                              goto {omega.name};")

        elif tag == "SPAN_ENTER":
            _, cs, gamma, omega = insn
            safe = cs.replc('\\', '\\\\').replc('"', '\\"')
            self.D(f"    int _span_save_δ = 0;")
            self.L(f"{prefix:30s} {{")
            self.L(f"{'':30s}     int δ = 0;")
            self.L(f"{'':30s}     while (Δ+δ < Ω && strchr(\"{safe}\", Σ[Δ+δ])) δ++;")
            self.L(f"{'':30s}     if (δ == 0) goto {omega.name};")
            self.L(f"{'':30s}     _span_save_δ = δ; Δ += δ;")
            self.L(f"{'':30s} }}                                 goto {gamma.name};")

        elif tag == "SPAN_RETREAT":
            _, cs, gamma, omega = insn
            self.L(f"{prefix:30s} if (_span_save_δ <= 1) {{ Δ -= _span_save_δ; goto {omega.name}; }}")
            self.L(f"{'':30s} _span_save_δ--; Δ--;              goto {gamma.name};")

        elif tag == "BREAK_ENTER":
            _, cs, gamma, omega = insn
            safe = cs.replc('\\', '\\\\').replc('"', '\\"')
            self.D(f"    int _break_save_δ = 0;")
            self.L(f"{prefix:30s} {{")
            self.L(f"{'':30s}     int δ = 0;")
            self.L(f"{'':30s}     while (Δ+δ < Ω && !strchr(\"{safe}\", Σ[Δ+δ])) δ++;")
            self.L(f"{'':30s}     if (Δ+δ >= Ω) goto {omega.name};")
            self.L(f"{'':30s}     _break_save_δ = Δ; Δ += δ;")
            self.L(f"{'':30s} }}                                 goto {gamma.name};")

        elif tag == "ARBNO_INIT":
            # α: depth=0, go try child immediately (no zero-mtch succeed)
            _, child_alpha = insn
            self.D(f"    int _arbno_depth = 0;")
            self.D(f"    int _arbno_stack[64];")
            self.L(f"{prefix:30s} _arbno_depth = 0;")
            self.L(f"{'':30s} _arbno_stack[0] = Δ;")
            self.L(f"{'':30s} goto {child_alpha.name};")

        elif tag == "ARBNO_EXTEND":
            # β: depth++, save cursor, try child again
            _, child_alpha = insn
            self.L(f"{prefix:30s} if (_arbno_depth + 1 >= 64) abort();")
            self.L(f"{'':30s} _arbno_stack[++_arbno_depth] = Δ;")
            self.L(f"{'':30s} goto {child_alpha.name};")

        elif tag == "ARBNO_POP":
            # child_ω: if depth==0 → ARBNO fails; else pop and resume child_β
            _, child_beta, omega = insn
            self.L(f"{prefix:30s} if (_arbno_depth <= 0) goto {omega.name};")
            self.L(f"{'':30s} Δ = _arbno_stack[_arbno_depth];")
            self.L(f"{'':30s} _arbno_depth--;")
            self.L(f"{'':30s} goto {child_beta.name};")

        elif tag in ("CALL_ALPHA", "CALL_BETA"):
            # Named pattern call — for now emit a comment placeholder
            _, name, gamma, omega = insn
            entry = "0" if tag == "CALL_ALPHA" else "1"
            self.L(f"{prefix:30s} /* CALL {name} entry={entry} → {gamma.name} / {omega.name} */")
            self.L(f"{'':30s} goto {gamma.name}; /* placeholder */")

        else:
            self.L(f"{prefix:30s} /* unknown prim: {tag} */")

    def generate(self, root_alpha: Label, succeed: Label, concede: Label,
                 subject: strv = "BlueGoldBirdFish") -> strv:
        safe_subj = subject.replc('\\', '\\\\').replc('"', '\\"')
        lines = []
        lines.append('#define __kernel')
        lines.append('#define __global')
        lines.append('#include <stdlib.h>')
        lines.append('#include <string.h>')
        lines.append('#include <stdio.h>')
        lines.append('/*-----------------------------------------------------------*/')
        lines.append('typedef struct { const char *σ; int δ; } str_t;')
        lines.append('/*-----------------------------------------------------------*/')
        lines.append('static inline str_t write_str(str_t s) {')
        lines.append('    printf("%.*s\\n", s.δ, s.σ); return s; }')
        lines.append('/*-----------------------------------------------------------*/')
        lines.append('__kernel void snobol(')
        lines.append('    __global const char *Σ,')
        lines.append('             const int   Ω)')
        lines.append('{')
        lines.append('    int Δ = 0;')
        lines.append('    goto %s;' % root_alpha.name)
        lines.append('    /*----- declarations ----*/')
        for d in self.decls:
            lines.append(d)
        lines.append('    /*----- code -----*/')
        for l in self.lines:
            lines.append('    ' + l)
        lines.append(f'    {succeed.name}:')
        lines.append('        printf("Success!\\n"); return;')
        lines.append(f'    {concede.name}:')
        lines.append('        printf("Failure.\\n"); return;')
        lines.append('}')
        lines.append('#ifdef __GNUC__')
        lines.append(f'static const char cszInput[] = "{safe_subj}";')
        lines.append('int main() {')
        lines.append('    snobol(cszInput, (int)sizeof(cszInput)-1);')
        lines.append('    return 0;')
        lines.append('}')
        lines.append('#endif')
        return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------------------
# Convenience: compile a pattern node to C source
# ---------------------------------------------------------------------------

def compile_to_c(node, subject: strv = "BlueGoldBirdFish",
                 pattern_name: strv = "root") -> strv:
    """Compile a pattern AST node to a complete C file."""
    import sys; sys.path.insert(0, os.path.dirname(__file__))

    succeed = Label("SUCCEED")
    concede = Label("CONCEDE")
    α = Label(f"{pattern_name}_α")
    β = Label(f"{pattern_name}_β")

    chunks: List[Chunk] = []
    lower_emit(node, α, β, succeed, concede, chunks)

    em = CByrdEmitter()
    em.emit_chunks(chunks)
    return em.generate(α, succeed, concede, subject=subject)


# ---------------------------------------------------------------------------
# Smoke test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    from byrd_ir import Seq, Alt, Lit, Pos, Rpos, Any, Arbno

    # The test_sno_1.c pattern:  POS(0) ARBNO('Bird' | 'Blue' | LEN(1)) RPOS(0)
    ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    pattern = Seq(Pos(0), Seq(
        Arbno(Alt(Alt(Lit("Bird"), Lit("Blue")), Any(ALPHA))),
        Rpos(0)
    ))

    src = compile_to_c(pattern, subject="BlueGoldBirdFish")
    print(src)

    # Write to /tmp and compile+run
    with open("/tmp/byrd_test.c", "w") as f:
        f.write(src)

    import subprocess
    r = subprocess.run(
        ["gcc", "-o", "/tmp/byrd_test", "/tmp/byrd_test.c"],
        capture_output=True, text=True
    )
    if r.returncode != 0:
        print("COMPILE ERROR:", r.stderr)
    else:
        out = subprocess.run(["/tmp/byrd_test"], capture_output=True, text=True)
        print("Output:", repr(out.stdout))
        print("PASS" if "Success" in out.stdout else "FAIL")
