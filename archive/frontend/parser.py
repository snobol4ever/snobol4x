"""
parser.py — snobol4x source parser (Sprint 14)

Parses a subset of SNOBOL4 into the IR node graph.

Sprint 14 statement forms:
  1. Immediate assignment:   OUTPUT = 'string'   or   var = 'string'
  2. Pattern mtch:          subject  pattern
  3. END — terminates the program

A SNOBOL4 statement has the structure:
  [label]  [subject]  [pattern]  [= replacement]  [: goto]

For Sprint 14 we handle:
  - String literals: 'text' or "text"
  - Variable references: IDENTIFIER
  - Concatenation: expr expr  (juxtaposition)
  - Assignment: OUTPUT = expr  or  var = expr

The parser produces an IR Graph where:
  - Each statement becomes a named pattern node
  - Execution order is a chain of Cat nodes
  - OUTPUT assignment becomes Assign(child, var='OUTPUT')
"""

import re
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'ir'))
from ir import Graph, Lit, Cat, Assign, Print, Alt, Ref


# ---------------------------------------------------------------------------
# Tokeniser
# ---------------------------------------------------------------------------

TK_IDENT   = 'IDENT'
TK_STRING  = 'STRING'
TK_EQ      = 'EQ'
TK_COLON   = 'COLON'
TK_LPAREN  = 'LPAREN'
TK_RPAREN  = 'RPAREN'
TK_NEWLINE = 'NEWLINE'
TK_EOF     = 'EOF'
TK_END     = 'END'

class Token:
    def __init__(self, kind, value, line):
        self.kind  = kind
        self.value = value
        self.line  = line
    def __repr__(self):
        return f"Token({self.kind}, {self.value!r})"


def tokenise(source):
    tokens = []
    lines  = source.splitlines()
    for lineno, line in enumerate(lines, 1):
        # Strip comment (lines starting with *)
        stripped = line.strip()
        if stripped.startswith('*'):
            continue
        if not stripped:
            continue

        pos = 0
        line_tokens = []

        while pos < len(line):
            # Skip whitespace (but track it as separator)
            if line[pos] == ' ' or line[pos] == '\t':
                pos += 1
                continue

            # String literal: 'text' or "text"
            if line[pos] in ("'", '"'):
                quote = line[pos]
                pos += 1
                start = pos
                while pos < len(line) and line[pos] != quote:
                    pos += 1
                value = line[start:pos]
                pos += 1  # closing quote
                line_tokens.append(Token(TK_STRING, value, lineno))
                continue

            # = assignment
            if line[pos] == '=':
                line_tokens.append(Token(TK_EQ, '=', lineno))
                pos += 1
                continue

            # : goto
            if line[pos] == ':':
                line_tokens.append(Token(TK_COLON, ':', lineno))
                pos += 1
                continue

            # ( )
            if line[pos] == '(':
                line_tokens.append(Token(TK_LPAREN, '(', lineno))
                pos += 1
                continue
            if line[pos] == ')':
                line_tokens.append(Token(TK_RPAREN, ')', lineno))
                pos += 1
                continue

            # Identifier / keyword
            if line[pos].isalpha() or line[pos] == '_' or line[pos] == '&':
                start = pos
                while pos < len(line) and (line[pos].isalnum() or line[pos] in ('_', '.')):
                    pos += 1
                word = line[start:pos]
                if word == 'END':
                    line_tokens.append(Token(TK_END, 'END', lineno))
                else:
                    line_tokens.append(Token(TK_IDENT, word, lineno))
                continue

            # Skip unknown character
            pos += 1

        if line_tokens:
            tokens.append(line_tokens)

    return tokens  # list of lines, each a list of tokens


# ---------------------------------------------------------------------------
# Expression parser
# ---------------------------------------------------------------------------

def parse_expr(toks, pos):
    """
    Parse a SNOBOL4 expression (Sprint 14 subset):
      expr ::= atom atom*        (concatenation by juxtaposition)
      atom ::= STRING | IDENT
    Returns (ir_node, new_pos) or (None, pos) if nothing parsed.
    """
    nodes = []
    while pos < len(toks):
        t = toks[pos]
        if t.kind == TK_STRING:
            nodes.append(Lit(t.value))
            pos += 1
        elif t.kind == TK_IDENT:
            # Variable reference — for Sprint 14, treat as Ref if
            # it names a known pattern, otherwise as a string placeholder.
            # We'll resolve refs in a second pass; for now emit Ref(name).
            nodes.append(Ref(t.value))
            pos += 1
        else:
            break

    if not nodes:
        return None, pos
    if len(nodes) == 1:
        return nodes[0], pos
    # Concatenate left to right
    node = nodes[0]
    for n in nodes[1:]:
        node = Cat(node, n)
    return node, pos


# ---------------------------------------------------------------------------
# Statement parser
# ---------------------------------------------------------------------------

class Statement:
    """Parsed SNOBOL4 statement."""
    def __init__(self, label, subject, pattern, replacement, goto, line):
        self.label       = label        # strv or None
        self.subject     = subject      # IR node or None
        self.pattern     = pattern      # IR node or None
        self.replacement = replacement  # IR node or None
        self.goto        = goto         # strv or None (label name)
        self.line        = line

    def __repr__(self):
        return (f"Stmt(label={self.label!r}, subj={self.subject}, "
                f"pat={self.pattern}, repl={self.replacement}, "
                f"goto={self.goto!r})")


def parse_statement(toks, lineno):
    """
    Parse one line of tokens into a Statement.

    SNOBOL4 statement structure:
      [label]  [subject [pattern]]  [= replacement]  [: goto]

    Sprint 14 heuristic:
    - If line is just END → return None (end of program)
    - If first token is IDENT followed immediately by EQ → assignment
      (no label, subject is the IDENT, replacement is the RHS expr)
    - Otherwise → subject is first expr, pattern is next expr (if any)
    """
    pos = 0

    # END statement
    if len(toks) == 1 and toks[0].kind == TK_END:
        return None

    label       = None
    subject     = None
    pattern     = None
    replacement = None
    goto        = None

    # Detect label: IDENT at position 0, but NOT followed by EQ or string.
    # A label is a bare identifier at the start of the line with nothing
    # following it on the same token group, OR followed by another statement.
    # Simple heuristic: if tok[0] is IDENT and tok[1] is NOT EQ and
    # tok[1] is NOT a STRING that could be the start of an expression,
    # treat tok[0] as a label only if the rest of the line has content.
    # For Sprint 14: label detection is deferred — we only need OUTPUT = ...

    # Pattern: IDENT = expr  (assignment)
    if (len(toks) >= 3
            and toks[0].kind == TK_IDENT
            and toks[1].kind == TK_EQ):
        subject = toks[0].value   # variable name as string
        pos = 2
        replacement, pos = parse_expr(toks, pos)
        return Statement(
            label=None,
            subject=subject,
            pattern=None,
            replacement=replacement,
            goto=None,
            line=lineno
        )

    # Fallback: subject [pattern] [= replacement] [: goto]
    subj_node, pos = parse_expr(toks, pos)
    subject = subj_node

    # Optional pattern
    if pos < len(toks) and toks[pos].kind not in (TK_EQ, TK_COLON):
        pat_node, pos = parse_expr(toks, pos)
        pattern = pat_node

    # Optional replacement
    if pos < len(toks) and toks[pos].kind == TK_EQ:
        pos += 1
        replacement, pos = parse_expr(toks, pos)

    # Optional goto
    if pos < len(toks) and toks[pos].kind == TK_COLON:
        pos += 1
        if pos < len(toks) and toks[pos].kind in (TK_IDENT, TK_LPAREN):
            goto = toks[pos].value
            pos += 1

    return Statement(
        label=label,
        subject=subject,
        pattern=pattern,
        replacement=replacement,
        goto=goto,
        line=lineno
    )


# ---------------------------------------------------------------------------
# Program → IR Graph
# ---------------------------------------------------------------------------

def parse_program(source):
    """
    Parse a snobol4x program into an IR Graph.

    Sprint 14 mapping:
      OUTPUT = 'expr'  →  Assign(expr, var='OUTPUT')
      var    = 'expr'  →  Assign(expr, var=var)      (stored, not printed)

    The graph has one root node 'MAIN' that chains all statements
    via Cat nodes in program order.
    """
    token_lines = tokenise(source)
    stmts = []

    for line_toks in token_lines:
        if not line_toks:
            continue
        lineno = line_toks[0].line
        stmt = parse_statement(line_toks, lineno)
        if stmt is None:
            break   # END
        stmts.append(stmt)

    if not stmts:
        raise ValueError("Empty program")

    g = Graph()

    # Build IR nodes for each statement
    stmt_nodes = []
    for i, stmt in enumerate(stmts):
        name = f"stmt{i+1}"

        if stmt.replacement is not None and isinstance(stmt.subject, strv):
            # Assignment: var = expr
            var = stmt.subject
            if var == 'OUTPUT' and isinstance(stmt.replacement, __import__('ir', fromlist=['Lit']).Lit):
                # OUTPUT = 'literal'  →  unconditional Print
                node = Print(expr=stmt.replacement.s)
            else:
                node = Assign(stmt.replacement, var=var)
            g.add(name, node)
            stmt_nodes.append(name)

        elif stmt.subject is not None and stmt.pattern is not None:
            # Match: subject pattern
            # For Sprint 14 — emit pattern mtch only (subject ignored)
            g.add(name, stmt.pattern)
            stmt_nodes.append(name)

        # else: skip (empty or unrecognised)

    if not stmt_nodes:
        raise ValueError("No compilable statements found")

    # Chain statements: inline all nodes directly into a Cat chain.
    # No Refs needed — all nodes are in the same compilation unit.
    if len(stmt_nodes) == 1:
        g2 = Graph()
        g2.add('MAIN', g.get(stmt_nodes[0]))
        return g2
    else:
        chain = g.get(stmt_nodes[0])
        for name in stmt_nodes[1:]:
            chain = Cat(chain, g.get(name))
        g2 = Graph()
        g2.add('MAIN', chain)
        return g2


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: parser.py <file.sno>", file=sys.stderr)
        sys.exit(1)

    source = open(sys.argv[1]).read()
    graph  = parse_program(source)

    print("/* IR graph: */")
    for name in graph.names():
        print(f"  {name}: {graph.get(name)}")
