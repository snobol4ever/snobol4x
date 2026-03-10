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
    s: str

@dataclass
class Any:
    charset: str

@dataclass
class Span:
    charset: str

@dataclass
class Break:
    charset: str

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
    var: str

@dataclass
class Print:
    """Unconditional output: prints a string literal, consumes no cursor.
    Always succeeds. Models SNOBOL4 OUTPUT = 'string' (Sprint 14)."""
    expr: str   # string value to print

@dataclass
class Ref:
    name: str  # forward reference resolved at codegen time

Node = Union[Lit, Any, Span, Break, Len, Pos, Rpos, Arb, Arbno,
             Alt, Cat, Assign, Print, Ref]


# ---------- graph ----------------------------------------------------

class Graph:
    """Named flat table of IR nodes.  Supports cycles via Ref."""

    def __init__(self):
        self._table: dict[str, Node] = {}
        self._order: list[str] = []

    def add(self, name: str, node: Node) -> "Graph":
        if name not in self._table:
            self._order.append(name)
        self._table[name] = node
        return self

    def get(self, name: str) -> Optional[Node]:
        return self._table.get(name)

    def names(self) -> list[str]:
        return list(self._order)

    def dot(self) -> str:
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
            if isinstance(n, Break): label += f'("{n.charset}")'
            lines.append(f'  {nid} [label="{label}"];')

        def walk(n) -> str:
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
