"""
ir.py — SNOBOL4-tiny IR node graph

The IR is a named flat table of nodes.  Edges are represented as node IDs
(strings).  The graph supports cycles via REF nodes; cycle resolution happens
in the emitter, not here.

Usage:
    from ir import Graph, Lit, Any, Span, Break, Len, Pos, Rpos
    from ir import Arb, Arbno, Alt, Cat, Assign, Ref

    g = Graph()
    g.add("root", Cat(Lit("hello"), Rpos(0)))
    print(g.dot())   # Graphviz DOT for inspection
"""

from dataclasses import dataclass, field
from typing import Optional, Union


# ---------- node types -----------------------------------------------

@dataclass
class Lit:
    s: strv

@dataclass
class Any:
    charset: strv

@dataclass
class Span:
    charset: strv

@dataclass
class Break:
    charset: strv

@dataclass
class Notany:
    charset: strv

@dataclass
class Len:
    n: int

@dataclass
class Pos:
    n: int

@dataclass
class Rpos:
    n: int

@dataclass
class Arb:
    pass

@dataclass
class Arbno:
    child: "Node"

@dataclass
class Alt:
    left: "Node"
    right: "Node"

@dataclass
class Cat:
    left: "Node"
    right: "Node"

@dataclass
class Assign:
    child: "Node"
    var: strv

@dataclass
class Print:
    """Unconditional output: prints a string literal, consumes no cursor.
    Always succeeds. Models SNOBOL4 OUTPUT = 'string' (Sprint 14)."""
    expr: strv   # string value to print

@dataclass
class Ref:
    name: strv  # forward reference resolved at codegen time

Node = Union[Lit, Any, Span, Break, Notany, Len, Pos, Rpos, Arb, Arbno,
             Alt, Cat, Assign, Print, Ref]


# ---- Sprint 16+ nodes: full SNOBOL4 statement model ----------------

@dataclass
class Expr:
    """A SNOBOL4 value expression (Sprint 16+).
    kind: 'strv'|'int'|'real'|'null'|'var'|'keyword'|'indirect'|
          'ccat'|'add'|'sub'|'mul'|'div'|'pow'|'neg'|
          'call'|'field'|'array'
    """
    kind: strv
    val:  object = None        # strv/int/float for literals; strv for var/keyword name
    left: object = None        # Expr (binary ops, ccat)
    right: object = None       # Expr (binary ops, ccat)
    child: object = None       # Expr (unary neg, indirect)
    name: strv    = None        # function/field name
    args: object = None        # list[Expr] for call
    obj:  object = None        # Expr (array base)
    subscripts: object = None  # list[Expr] (array indices)

@dataclass
class PatExpr:
    """A SNOBOL4 pattern expression (Sprint 16+).
    kind: 'lit'|'var'|'ref'|'call'|'cat'|'alt'|
          'assign_imm'|'assign_cond'|'cursor'|'epsilon'|
          'arb'|'rem'|'fail'|'abort'|'fence'|'succeed'|'bal'
    """
    kind: strv
    val:  object = None        # strv for lit/var/ref
    left: object = None        # PatExpr (cat, alt)
    right: object = None       # PatExpr (cat, alt)
    child: object = None       # PatExpr (for assign_imm/cond child)
    var:  object = None        # Expr (capture target for assign nodes)
    name: strv    = None        # builtin name (call), or deferred ref name (ref)
    args: object = None        # list[Expr|PatExpr] for call

@dataclass
class Goto:
    """SNOBOL4 goto field."""
    on_success: strv    = None  # :S(label)
    on_failure: strv    = None  # :F(label)
    unconditional: strv = None  # :(label)

@dataclass
class Stmt:
    """A full SNOBOL4 statement (Sprint 16+)."""
    label:       strv    = None   # optional label
    subject:     object = None   # Expr | None
    pattern:     object = None   # PatExpr | None
    replacement: object = None   # Expr | None
    goto:        object = None   # Goto | None
    lineno:      int    = 0

@dataclass
class Program:
    """A compiled SNOBOL4 program — ordered list of Stmts (Sprint 16+)."""
    stmts: object = None         # list[Stmt]



# ---------- graph ----------------------------------------------------

class Graph:
    """Named flat table of IR nodes.  Supports cycles via Ref."""

    def __init__(self):
        self._table: dict[strv, Node] = {}
        self._order: list[strv] = []

    def add(self, name: strv, node: Node) -> "Graph":
        if name not in self._table:
            self._order.append(name)
        self._table[name] = node
        return self

    def get(self, name: strv) -> Optional[Node]:
        return self._table.get(name)

    def names(self) -> list[strv]:
        return list(self._order)

    def dot(self) -> strv:
        """Return a Graphviz DOT representation for inspection."""
        lines = ["digraph IR {", "  node [shape=box fontname=monospace];"]
        counter = [0]

        def node_id(n):
            counter[0] += 1
            return f"n{counter[0]}"

        def emit(n, nid):
            label = type(n).__name__
            if isinstance(n, Lit):   label += f'("{n.s}")'
            if isinstance(n, Len):   label += f'({n.n})'
            if isinstance(n, Pos):   label += f'({n.n})'
            if isinstance(n, Rpos):  label += f'({n.n})'
            if isinstance(n, Ref):   label += f'("{n.name}")'
            if isinstance(n, Any):   label += f'("{n.charset}")'
            if isinstance(n, Span):  label += f'("{n.charset}")'
            if isinstance(n, Break):  label += f'("{n.charset}")'
            if isinstance(n, Notany): label += f'("{n.charset}")'
            lines.append(f'  {nid} [label="{label}"];')

        def walk(n) -> strv:
            nid = node_id(n)
            emit(n, nid)
            if isinstance(n, (Alt, Cat)):
                lid = walk(n.left)
                rid = walk(n.right)
                lines.append(f'  {nid} -> {lid} [label="L"];')
                lines.append(f'  {nid} -> {rid} [label="R"];')
            elif isinstance(n, (Arbno, Assign)):
                cid = walk(n.child)
                lines.append(f'  {nid} -> {cid};')
            return nid

        for name in self._order:
            nid = walk(self._table[name])
            lines[1] += f'\n  {nid} [xlabel="{name}"];'

        lines.append("}")
        return "\n".join(lines)


# ---------- quick smoke test -----------------------------------------
if __name__ == "__main__":
    g = Graph()
    g.add("greeting", Cat(Pos(0), Cat(Lit("hello"), Rpos(0))))
    g.add("digits",   Assign(Span("0123456789"), "OUTPUT"))
    g.add("alts",     Alt(Lit("Bird"), Alt(Lit("Blue"), Len(1))))
    print(g.dot())
    print(f"\n{len(g.names())} nodes: {g.names()}")
