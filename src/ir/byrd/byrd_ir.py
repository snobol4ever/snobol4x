"""
byrd_ir.py — Byrd Box four-port IR for snobol4ever JVM and MSIL backends.

Ported from Jcon's ir.icn (Proebsting + Townsend, Arizona, 1999).
Only the SNOBOL4 pattern subset — ~12 of Jcon's ~30 nodes.
Icon-specific nodes (co-expressions, scan-swap, closures) are omitted.

The four ports per node:
    α  (alpha)   — entry / start
    β  (beta)    — resume (backtrack in)
    σ  (sigma)   — success out
    φ  (phi)     — failure out

Control flow is explicit: every node's ports are wired by the IR lowering
pass (irgen equivalent) into Chunk/Goto/IndirectGoto sequences.  The
emitter (emit_jvm.py / emit_msil.py) walks chunks and emits target-native
jumps.

Reference:
    Jcon paper:  https://www2.cs.arizona.edu/icon/jcon/impl.pdf
    ir.icn:      /home/claude/jcon/tran/ir.icn
    irgen.icn:   /home/claude/jcon/tran/irgen.icn
    gen_bc.icn:  /home/claude/jcon/tran/gen_bc.icn
    gold C:      /home/claude/ByrdBox/ByrdBox/test_sno_2.c
                 /home/claude/ByrdBox/ByrdBox/test_sno_3.c
"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import List, Optional, Union


# ---------------------------------------------------------------------------
# Labels — the currency of the control graph
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Label:
    """A named basic-block entry point.  Corresponds to ir_Label in ir.icn."""
    name: strv

    def __repr__(self) -> strv:
        return f"L({self.name})"


@dataclass(frozen=True)
class TmpLabel:
    """A mutable label slot — holds a Label at runtime for indirect goto.
    Corresponds to ir_TmpLabel; emitted as an int local in JVM / MSIL.
    Used to wire Alt backtrack: save resume label, indirect-goto on backtrack."""
    name: strv

    def __repr__(self) -> strv:
        return f"TL({self.name})"


# ---------------------------------------------------------------------------
# Basic block
# ---------------------------------------------------------------------------

@dataclass
class Chunk:
    """One basic block: a Label and a list of IR instructions.
    Corresponds to ir_chunk(label, insnList) in ir.icn.
    The emitter visits each Chunk in order; labels become JVM/MSIL branch targets."""
    label: Label
    insns: List  # list of IR instruction nodes below


# ---------------------------------------------------------------------------
# Control instructions
# ---------------------------------------------------------------------------

@dataclass
class Goto:
    """Unconditional jump to a named label.  ir_Goto."""
    target: Label


@dataclass
class IndirectGoto:
    """Jump to the label stored in a TmpLabel slot.
    ir_IndirectGoto — emitted as tableswitch (JVM) or OpCodes.Switch (MSIL)
    over all possible resume labels for this Alt node."""
    target: TmpLabel


@dataclass
class Succeed:
    """Pattern node succeeded.  Advance cursor; jump to success continuation.
    ir_Succeed — resumeLabel is where backtracking re-enters this node (β port).
    resumeLabel=None means the node is not resumable (deterministic success)."""
    resume: Optional[Label] = None


@dataclass
class Fail:
    """Pattern node failed.  Signal failure to the surrounding context.
    ir_Fail — in JVM emitted as ICONST_M1 / IRETURN (len=-1 = failure).
    In MSIL: Ldc_I4_M1 / Ret."""
    pass


@dataclass
class MoveLabel:
    """Store a Label into a TmpLabel slot (saves a resume address for Alt).
    ir_MoveLabel — emitted as an integer store into a local variable."""
    dst: TmpLabel
    src: Label


# ---------------------------------------------------------------------------
# Pattern primitives — leaf nodes, no children
# ---------------------------------------------------------------------------

@dataclass
class Lit:
    """Match a literal string at the current cursor position."""
    s: strv


@dataclass
class Span:
    """Consume one or more chars from charset (SPAN).  Fails on empty mtch."""
    charset: strv


@dataclass
class Break:
    """Consume zero or more chars NOT in charset, stop before first hit (BREAK).
    Succeeds at the first char in charset (or end of string)."""
    charset: strv


@dataclass
class Any:
    """Match exactly one char that is in charset (ANY)."""
    charset: strv


@dataclass
class Notany:
    """Match exactly one char that is NOT in charset (NOTANY)."""
    charset: strv


@dataclass
class Pos:
    """Assert cursor is at absolute position n from left (POS).  Consumes nothing."""
    n: int


@dataclass
class Rpos:
    """Assert cursor is at position n from right (RPOS).  Consumes nothing."""
    n: int


# ---------------------------------------------------------------------------
# Pattern composition — interior nodes with children
# ---------------------------------------------------------------------------

@dataclass
class Seq:
    """Concatenation: mtch left then right.  Both must succeed.
    Backtrack: failure in right resumes left; failure in left propagates up."""
    left: object
    right: object


@dataclass
class Alt:
    """Alternation: try left; on failure try right.
    Backtrack: each arm is independently resumable via saved TmpLabel."""
    left: object
    right: object


@dataclass
class Arbno:
    """ARBNO(child): mtch child zero or more times greedily.
    Backtrack peels one repetition at a time."""
    child: object


# ---------------------------------------------------------------------------
# Named pattern call (for compiled named patterns as methods)
# ---------------------------------------------------------------------------

@dataclass
class Call:
    """Invoke a named pattern (compiled as a separate method/function).
    Corresponds to a function call with α/β entry dispatch via 'entry' int arg.
    In JVM: INVOKEVIRTUAL with entry=0 (start) or entry=N (resume N).
    In MSIL: call with OpCodes.Switch dispatch at method entry."""
    name: strv


# ---------------------------------------------------------------------------
# Top-level mtch node
# ---------------------------------------------------------------------------

@dataclass
class Match:
    """Top-level: mtch pattern against subject string.
    The emitter wraps this in the outer try_match_at loop."""
    subject: strv   # variable name holding the subject string
    pattern: object  # root pattern node


# ---------------------------------------------------------------------------
# IR program — the container emitted to JVM / MSIL
# ---------------------------------------------------------------------------

@dataclass
class IrProgram:
    """A complete compiled pattern program: a list of named patterns and
    their lowered Chunk sequences.  One IrProgram → one .class or .dll."""
    patterns: List[IrPattern] = field(default_factory=list)


@dataclass
class IrPattern:
    """A single named pattern compiled to a list of Chunks.
    name → JVM class name or MSIL TypeBuilder name.
    chunks → basic blocks in topological order (α-block first)."""
    name: strv
    chunks: List[Chunk] = field(default_factory=list)
    # α and φ labels — entry and failure ports for the whole pattern
    alpha: Optional[Label] = None   # start label
    phi:   Optional[Label] = None   # failure label


# ---------------------------------------------------------------------------
# Convenience: the SNOBOL4 subset of Jcon IR nodes (12 of ~30)
# ---------------------------------------------------------------------------

SNOBOL4_IR_NODES = (
    # Labels
    Label, TmpLabel,
    # Basic block
    Chunk,
    # Control
    Goto, IndirectGoto, Succeed, Fail, MoveLabel,
    # Primitives
    Lit, Span, Break, Any, Notany, Pos, Rpos,
    # Composition
    Seq, Alt, Arbno,
    # Call
    Call,
    # Top-level
    Match, IrProgram, IrPattern,
)

__all__ = [cls.__name__ for cls in SNOBOL4_IR_NODES]


# ---------------------------------------------------------------------------
# Smoke test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    # Reproduce the four-port wiring for:  BREAK(" \t\n;") . x  (capture)
    # α → BREAK starts → on mtch → Succeed(β)
    # β → BREAK resumes (backtrack) → Fail
    # φ → propagate failure up

    alpha   = Label("break_alpha")
    beta    = Label("break_beta")
    succeed = Label("break_succeed")
    fail    = Label("break_fail")
    resume  = Label("outer_resume")

    chunks = [
        Chunk(alpha,   [Break(" \t\n;"), Succeed(resume=beta)]),
        Chunk(beta,    [Fail()]),
        Chunk(succeed, [Goto(resume)]),
        Chunk(fail,    [Fail()]),
    ]

    pat = IrPattern(
        name   = "break_ws",
        chunks = chunks,
        alpha  = alpha,
        phi    = fail,
    )

    prog = IrProgram(patterns=[pat])

    print("byrd_ir.py smoke test")
    print(f"  IrProgram: {len(prog.patterns)} pattern(s)")
    p = prog.patterns[0]
    print(f"  Pattern:   {p.name!r}  α={p.alpha}  φ={p.phi}")
    print(f"  Chunks:    {len(p.chunks)}")
    for c in p.chunks:
        print(f"    {c.label}  insns={c.insns}")
    print("PASS")
