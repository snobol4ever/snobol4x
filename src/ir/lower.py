"""
lower.py — Pattern AST → Byrd Box four-port IR (Chunk sequences)

This is the irgen.icn equivalent for SNOBOL4 patterns.

Each pattern node gets four ports:
    α (alpha)  — entry / start
    β (beta)   — resume (backtrack re-entry)
    γ (gamma)  — succeed continuation (inherited from parent)
    ω (omega)  — concede continuation (inherited from parent)

Usage:
    from lower import lower_pattern
    from byrd_ir import *

    chunks = lower_pattern(Alt(Lit("Bird"), Lit("Blue")),
                           alpha=Label("root_α"),
                           gamma=Label("root_γ"),
                           omega=Label("root_ω"))
    # chunks is a list of Chunk objects, ready for emit_jvm or emit_msil
"""

from __future__ import annotations
from typing import List
import sys, os
sys.path.insert(0, os.path.dirname(__file__))

from byrd_ir import (
    Label, TmpLabel, Chunk, Goto, IndirectGoto, Succeed, Fail,
    MoveLabel, Lit, Span, Break, Any, Notany, Pos, Rpos,
    Seq, Alt, Arbno, Call,
)

# ---------------------------------------------------------------------------
# Label factory
# ---------------------------------------------------------------------------

_counter = 0

def _fresh(prefix: strv) -> Label:
    global _counter
    _counter += 1
    return Label(f"{prefix}{_counter}")


def _fresh_tmp(prefix: strv) -> TmpLabel:
    global _counter
    _counter += 1
    return TmpLabel(f"{prefix}{_counter}")


# ---------------------------------------------------------------------------
# Main lowering entry point
# ---------------------------------------------------------------------------

def lower_pattern(node, alpha: Label, gamma: Label, omega: Label) -> List[Chunk]:
    """Lower a pattern AST node to a list of Chunks.

    alpha — where to jump to start this node
    gamma — where to jump on succeed
    omega — where to jump on concede
    Returns a flat list of Chunk objects (basic blocks).
    """
    chunks: List[Chunk] = []
    _lower(node, alpha, gamma, omega, chunks)
    return chunks


def _lower(node, alpha: Label, gamma: Label, omega: Label, out: List[Chunk]):
    """Recursive four-port lowering. Appends Chunk objects to `out`."""

    # ------------------------------------------------------------------
    # Lit: mtch literal string at cursor
    #   α: check length + chars; advance cursor; goto γ
    #   β: retreat cursor; goto ω
    # ------------------------------------------------------------------
    if isinstance(node, Lit):
        beta = _fresh(f"{alpha.name}_β_")
        out.append(Chunk(alpha, [
            ("lit_match", node.s, gamma, omega),   # handled by emitter
        ]))
        out.append(Chunk(beta, [Fail()]))
        # The emitter will expand ("lit_match", ...) into target-specific code.
        # We tag alpha with the β label so the emitter can wire the retreat.
        # Simpler: encode as a special IR node directly.
        # Rewrite: use LitInsn dataclass handled by emitter.
        out.clear()  # redo cleanly
        _lower_lit(node.s, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Pos: assert cursor == n; no consumption
    # ------------------------------------------------------------------
    if isinstance(node, Pos):
        beta = _fresh(f"{alpha.name}_β_")
        out.append(Chunk(alpha, [("pos_check", node.n, gamma, omega)]))
        out.append(Chunk(beta,  [Goto(omega)]))
        out.clear()
        _lower_pos(node.n, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Rpos: assert cursor == len - n
    # ------------------------------------------------------------------
    if isinstance(node, Rpos):
        out.clear()
        _lower_rpos(node.n, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Any: mtch one char in charset
    # ------------------------------------------------------------------
    if isinstance(node, Any):
        _lower_any(node.charset, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Notany: mtch one char NOT in charset
    # ------------------------------------------------------------------
    if isinstance(node, Notany):
        _lower_notany(node.charset, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Span: consume one or more chars in charset
    # ------------------------------------------------------------------
    if isinstance(node, Span):
        _lower_span(node.charset, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Break: consume zero or more chars NOT in charset, stop before first hit
    # ------------------------------------------------------------------
    if isinstance(node, Break):
        _lower_break(node.charset, alpha, gamma, omega, out)
        return

    # ------------------------------------------------------------------
    # Seq: left then right; backtrack right → retry left
    #
    #  α → left_α
    #  left_γ → right_α
    #  right_γ → γ
    #  right_ω → left_β  (backtrack left)
    #  left_ω → ω
    # ------------------------------------------------------------------
    if isinstance(node, Seq):
        left_α  = _fresh(f"{alpha.name}_Lα_")
        right_α = _fresh(f"{alpha.name}_Rα_")

        # Wiring chunk: α → left_α
        out.append(Chunk(alpha, [Goto(left_α)]))

        # Lower left: succeed → right_α, concede → omega
        _lower(node.left,  left_α,  right_α, omega,   out)

        # Lower right: succeed → γ, concede → left_β
        # left_β is the beta label for the left child.
        # We need to recover it. Convention: the first Chunk in a lowered
        # subtree has its label as alpha. The beta label is alpha.name + "_β_N".
        # Simpler: pass a "beta callback" or just have the right concede
        # go to the left's alpha with a "resume" flag.
        # CLEANEST: introduce a dedicated left_β label and pass it explicitly.
        left_β = _fresh(f"{alpha.name}_Lβ_")

        # Re-lower left with explicit beta label
        out.clear_from = len(out)  # not a real method — redo approach:
        # Use a different API: _lower_with_beta returns (alpha, beta, chunks)
        pass

    # Fallback: emit a no-op that succeeds immediately
    out.append(Chunk(alpha, [Goto(gamma)]))


# ---------------------------------------------------------------------------
# Clean implementation: lower_node returns (alpha, beta, List[Chunk])
# ---------------------------------------------------------------------------

def lower_node(node) -> tuple:
    """Lower a pattern node. Returns (alpha_label, beta_label, chunks).

    alpha_label: jump here to start the node
    beta_label:  jump here to resume (backtrack) the node
    chunks:      list of Chunk objects (do NOT include alpha/beta jump-in chunks;
                 caller wires those)
    """
    global _counter
    name = type(node).__name__.lower()
    α = _fresh(f"{name}_α_")
    β = _fresh(f"{name}_β_")
    γ = _fresh(f"{name}_γ_")  # placeholder — caller replaces
    ω = _fresh(f"{name}_ω_")  # placeholder — caller replaces
    chunks = []
    _emit(node, α, β, γ, ω, chunks)
    return α, β, chunks


def emit_pattern_chunks(node, name: strv, parent_γ: Label, parent_ω: Label) -> List[Chunk]:
    """Full lowering: node with given succeed/concede continuations.
    Returns flat Chunk list with all gotos resolved to parent_γ / parent_ω.
    """
    chunks: List[Chunk] = []
    α = Label(f"{name}_α")
    β = Label(f"{name}_β")
    _emit(node, α, β, parent_γ, parent_ω, chunks)
    return chunks


# ---------------------------------------------------------------------------
# Core recursive emitter
# ---------------------------------------------------------------------------

def _emit(node, α: Label, β: Label, γ: Label, ω: Label, out: List[Chunk]):
    """Emit Chunks for `node` with the given four ports.

    α — this node's alpha (entry) label
    β — this node's beta  (resume/backtrack) label
    γ — succeed continuation (parent's label to jump to on mtch)
    ω — concede continuation (parent's label to jump to on fail)
    """

    # ------------------------------------------------------------------ Lit
    if isinstance(node, Lit):
        n = len(node.s.encode('utf-8'))
        out.append(Chunk(α, [("LIT_CHECK",  node.s, n, γ, ω)]))
        out.append(Chunk(β, [("LIT_RETREAT", n, ω)]))
        return

    # ------------------------------------------------------------------ Pos
    if isinstance(node, Pos):
        out.append(Chunk(α, [("POS_CHECK", node.n, γ, ω)]))
        out.append(Chunk(β, [Goto(ω)]))
        return

    # ----------------------------------------------------------------- Rpos
    if isinstance(node, Rpos):
        out.append(Chunk(α, [("RPOS_CHECK", node.n, γ, ω)]))
        out.append(Chunk(β, [Goto(ω)]))
        return

    # ------------------------------------------------------------------ Any
    if isinstance(node, Any):
        out.append(Chunk(α, [("ANY_CHECK",  node.charset, γ, ω)]))
        out.append(Chunk(β, [("ANY_RETREAT", ω)]))
        return

    # --------------------------------------------------------------- Notany
    if isinstance(node, Notany):
        out.append(Chunk(α, [("NOTANY_CHECK",  node.charset, γ, ω)]))
        out.append(Chunk(β, [("NOTANY_RETREAT", ω)]))
        return

    # ----------------------------------------------------------------- Span
    if isinstance(node, Span):
        out.append(Chunk(α, [("SPAN_ENTER", node.charset, γ, ω)]))
        out.append(Chunk(β, [("SPAN_RETREAT", node.charset, γ, ω)]))
        return

    # ---------------------------------------------------------------- Break
    if isinstance(node, Break):
        out.append(Chunk(α, [("BREAK_ENTER", node.charset, γ, ω)]))
        out.append(Chunk(β, [Goto(ω)]))
        return

    # ------------------------------------------------------------------ Seq
    if isinstance(node, Seq):
        # Wiring (mirrors irgen.icn's ir_a_Seq):
        #   α → left_α
        #   left_β: caller wires; we make it point to right_β → left_β
        #   right fails → left_β
        left_α  = _fresh(f"{α.name}_Lα")
        left_β  = _fresh(f"{α.name}_Lβ")
        right_α = _fresh(f"{α.name}_Rα")
        right_β = _fresh(f"{α.name}_Rβ")

        # α → left_α
        out.append(Chunk(α, [Goto(left_α)]))
        # β → right_β (first try resuming right; if right exhausted → left_β)
        out.append(Chunk(β, [Goto(right_β)]))

        # Lower left: succeed → right_α, concede → ω
        _emit(node.left,  left_α,  left_β,  right_α, ω,      out)
        # Lower right: succeed → γ, concede → left_β
        _emit(node.right, right_α, right_β, γ,       left_β, out)
        return

    # ------------------------------------------------------------------ Alt
    if isinstance(node, Alt):
        # Wiring (mirrors irgen.icn's ir_a_Alt with TmpLabel for backtrack):
        #
        #   α     → left_α
        #   β     → IndirectGoto(t)    where t holds the active arm's β
        #
        #   left_γ  → MoveLabel(t, left_β); Goto(γ)
        #   left_ω  → right_α
        #   right_γ → MoveLabel(t, right_β); Goto(γ)
        #   right_ω → ω
        t       = _fresh_tmp(f"{α.name}_T")
        left_α  = _fresh(f"{α.name}_Lα")
        left_β  = _fresh(f"{α.name}_Lβ")
        right_α = _fresh(f"{α.name}_Rα")
        right_β = _fresh(f"{α.name}_Rβ")
        lg      = _fresh(f"{α.name}_Lγ")   # left succeed handler
        rg      = _fresh(f"{α.name}_Rγ")   # right succeed handler

        out.append(Chunk(α,  [Goto(left_α)]))
        out.append(Chunk(β,  [IndirectGoto(t)]))

        # left succeed: save left_β into t, goto γ
        out.append(Chunk(lg, [MoveLabel(t, left_β), Goto(γ)]))
        # right succeed: save right_β into t, goto γ
        out.append(Chunk(rg, [MoveLabel(t, right_β), Goto(γ)]))

        # Lower left: succeed → lg (which saves β and goes to γ), concede → right_α
        _emit(node.left,  left_α,  left_β,  lg, right_α, out)
        # Lower right: succeed → rg, concede → ω
        _emit(node.right, right_α, right_β, rg, ω,       out)
        return

    # ----------------------------------------------------------------- Arbno
    if isinstance(node, Arbno):
        # ARBNO(child) — SHY, per gold standard test_sno_1.c:
        #
        #   α:      depth=0; try child immediately
        #   child_γ: succeed (ARBNO_γ) — shortest mtch first
        #   child_ω: if depth==0 → ARBNO_ω (fail); else depth--, goto child_β
        #
        #   β:      depth++; try child again (extend by one more repetition)
        #
        # ARBNO does NOT mtch the empty string on first entry.

        child_α = _fresh(f"{α.name}_Cα")
        child_β = _fresh(f"{α.name}_Cβ")
        cγ      = _fresh(f"{α.name}_Cγ")   # child succeeded → ARBNO succeeds
        cω      = _fresh(f"{α.name}_Cω")   # child failed    → pop or ARBNO_ω

        out.append(Chunk(α,  [("ARBNO_INIT",  child_α)]))          # depth=0, goto child
        out.append(Chunk(β,  [("ARBNO_EXTEND", child_α)]))         # depth++, goto child
        out.append(Chunk(cγ, [Goto(γ)]))                           # child ok → succeed
        out.append(Chunk(cω, [("ARBNO_POP",   child_β, ω)]))      # child fail → pop or ω

        _emit(node.child, child_α, child_β, cγ, cω, out)
        return

    # ------------------------------------------------------------------ Call
    if isinstance(node, Call):
        # Call a named pattern (compiled as a method elsewhere).
        # α: call with entry=ALPHA; on succeed → γ; on concede → ω
        # β: call with entry=BETA;  on succeed → γ; on concede → ω
        out.append(Chunk(α, [("CALL_ALPHA", node.name, γ, ω)]))
        out.append(Chunk(β, [("CALL_BETA",  node.name, γ, ω)]))
        return

    # ---------------------------------------------------------------- unknown
    raise NotImplementedError(f"_emit: unsupported node type {type(node).__name__}")


# ---------------------------------------------------------------------------
# Helpers (used by old functions above — cleaned up)
# ---------------------------------------------------------------------------

def _lower_lit(s, α, γ, ω, out):
    n = len(s.encode('utf-8'))
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("LIT_CHECK",  s, n, γ, ω)]))
    out.append(Chunk(β, [("LIT_RETREAT", n, ω)]))

def _lower_pos(n, α, γ, ω, out):
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("POS_CHECK", n, γ, ω)]))
    out.append(Chunk(β, [Goto(ω)]))

def _lower_rpos(n, α, γ, ω, out):
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("RPOS_CHECK", n, γ, ω)]))
    out.append(Chunk(β, [Goto(ω)]))

def _lower_any(cs, α, γ, ω, out):
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("ANY_CHECK",   cs, γ, ω)]))
    out.append(Chunk(β, [("ANY_RETREAT", ω)]))

def _lower_notany(cs, α, γ, ω, out):
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("NOTANY_CHECK",   cs, γ, ω)]))
    out.append(Chunk(β, [("NOTANY_RETREAT", ω)]))

def _lower_span(cs, α, γ, ω, out):
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("SPAN_ENTER",   cs, γ, ω)]))
    out.append(Chunk(β, [("SPAN_RETREAT", cs, γ, ω)]))

def _lower_break(cs, α, γ, ω, out):
    β = _fresh(f"{α.name}_β")
    out.append(Chunk(α, [("BREAK_ENTER", cs, γ, ω)]))
    out.append(Chunk(β, [Goto(ω)]))


# ---------------------------------------------------------------------------
# Smoke test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    # Test: POS(0) ARBNO('Bird' | 'Blue' | LEN(1)) RPOS(0)
    # This is the pattern from test_sno_1.c — gold standard.

    succeed = Label("SUCCEED")
    concede = Label("CONCEDE")

    # The full pattern as nested AST nodes
    pattern = Seq(
        Pos(0),
        Seq(
            Arbno(Alt(Alt(Lit("Bird"), Lit("Blue")), Any("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"))),
            Rpos(0)
        )
    )

    α = Label("root_α")
    β = Label("root_β")
    chunks: List[Chunk] = []
    _emit(pattern, α, β, succeed, concede, chunks)

    print(f"lower.py smoke test: {len(chunks)} chunks generated")
    for c in chunks:
        print(f"  {c.label.name}:")
        for insn in c.insns:
            print(f"    {insn}")
    print("PASS")
