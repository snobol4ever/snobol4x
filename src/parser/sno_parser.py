"""
sno_parser.py — Full SNOBOL4 parser (Sprint 16)

Parses complete SNOBOL4 source (with -INCLUDE expansion) into a Program
of Stmt nodes, each holding optional label/subject/pattern/replacement/goto.

SNOBOL4 statement structure:
  [label]  [subject  [pattern]  [= replacement]]  [: goto]

Continuation lines begin with '+' or '.' in column 1.

Expression grammar (value expressions):
  expr     ::= term (concat_op term)*
  concat_op::= (juxtaposition — blank between terms)
  term     ::= factor (('+'|'-') factor)*
  factor   ::= atom (('*'|'/'|'**') atom)*
  atom     ::= unary_minus? primary
  primary  ::= string_literal
             | integer_literal
             | real_literal
             | IDENT '(' args ')' '(' subscripts ')'  (2D subscript)
             | IDENT '(' args ')'                      (call or 1D subscript)
             | IDENT '[' subscripts ']'               (subscript)
             | IDENT '[' i ',' j ']'                  (2D subscript)
             | '$' primary                             (indirect)
             | '&' IDENT                               (keyword)
             | IDENT                                   (variable)
             | '(' expr ')'

Pattern expression grammar:
  pat      ::= pat_term ('|' pat_term)*
  pat_term ::= pat_factor+                              (concatenation)
  pat_factor::= pat_atom ('$'|'.') expr                 (capture)
             | pat_atom
  pat_atom ::= '*' IDENT                               (deferred ref)
             | string_literal                          (literal pattern)
             | IDENT '(' pat_args ')'                  (pattern call)
             | IDENT                                   (variable pattern)
             | '(' pat ')'
             | epsilon (empty, always succeeds)

GOTO field:   :(label)  :S(label)  :F(label)  combinations
"""

import re
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'ir'))
from ir import (Graph, Lit, Cat, Alt, Assign, Print, Ref,
                Expr, PatExpr, Goto, Stmt, Program)

# ---------------------------------------------------------------------------
# Tokeniser
# ---------------------------------------------------------------------------

# Token kinds
TK = dict(
    STR='STR',          # 'text' or "text"
    INT='INT',          # 123
    REAL='REAL',        # 1.5 or 1e3
    IDENT='IDENT',      # identifier
    EQ='EQ',            # =
    COLON='COLON',      # :
    LPAREN='LPAREN',    # (
    RPAREN='RPAREN',    # )
    LBRACKET='LBRACKET',# [
    RBRACKET='RBRACKET',# ]
    STAR='STAR',        # *  (deferred ref prefix OR multiply)
    PLUS='PLUS',        # +
    MINUS='MINUS',      # -
    SLASH='SLASH',      # /
    STARSTAR='STARSTAR',# **
    PIPE='PIPE',        # |
    DOLLAR='DOLLAR',    # $
    DOT='DOT',          # .
    AMP='AMP',          # &
    COMMA='COMMA',      # ,
    SEMI='SEMI',        # ;
    NEWLINE='NEWLINE',
    EOF='EOF',
)


class Token:
    __slots__ = ('kind', 'val', 'line')
    def __init__(self, kind, val, line):
        self.kind = kind
        self.val  = val
        self.line = line
    def __repr__(self):
        return f'Token({self.kind},{self.val!r}@{self.line})'


def _tokenise_line(line, lineno, tokens):
    """Tokenise one logical line (already continuation-joined) into tokens list."""
    i = 0
    n = len(line)
    while i < n:
        c = line[i]

        # Skip whitespace (but note it as a separator — important for concat)
        if c in ' \t':
            # Emit a synthetic SPACE token so the parser can detect juxtaposition
            if tokens and tokens[-1].kind not in ('NEWLINE', 'SPACE'):
                tokens.append(Token('SPACE', ' ', lineno))
            i += 1
            continue

        # Skip inline comment ; * ...
        if c == ';':
            break

        # String literal
        if c in ("'", '"'):
            quote = c
            i += 1
            start = i
            while i < n and line[i] != quote:
                i += 1
            val = line[start:i]
            if i < n:
                i += 1  # closing quote
            tokens.append(Token(TK['STR'], val, lineno))
            continue

        # ** before *
        if c == '*' and i+1 < n and line[i+1] == '*':
            tokens.append(Token(TK['STARSTAR'], '**', lineno))
            i += 2
            continue

        # Single-char operators
        single = {
            '=': TK['EQ'],
            ':': TK['COLON'],
            '(': TK['LPAREN'],
            ')': TK['RPAREN'],
            '[': TK['LBRACKET'],
            ']': TK['RBRACKET'],
            '*': TK['STAR'],
            '+': TK['PLUS'],
            '-': TK['MINUS'],
            '/': TK['SLASH'],
            '|': TK['PIPE'],
            '$': TK['DOLLAR'],
            '.': TK['DOT'],
            '~': TK['DOT'],   # ~ is alias for . (conditional assignment)
            '&': TK['AMP'],
            ',': TK['COMMA'],
        }
        if c in single:
            tokens.append(Token(single[c], c, lineno))
            i += 1
            continue

        # Integer or real
        if c.isdigit():
            start = i
            while i < n and line[i].isdigit():
                i += 1
            # Check for real: digits . digits or digits e±digits
            if i < n and line[i] == '.' and i+1 < n and line[i+1].isdigit():
                i += 1
                while i < n and line[i].isdigit():
                    i += 1
                tokens.append(Token(TK['REAL'], float(line[start:i]), lineno))
            elif i < n and line[i] in ('e', 'E'):
                i += 1
                if i < n and line[i] in ('+', '-'):
                    i += 1
                while i < n and line[i].isdigit():
                    i += 1
                tokens.append(Token(TK['REAL'], float(line[start:i]), lineno))
            else:
                tokens.append(Token(TK['INT'], int(line[start:i]), lineno))
            continue

        # Identifier
        if c.isalpha() or c == '_':
            start = i
            while i < n and (line[i].isalnum() or line[i] in '_'):
                i += 1
            tokens.append(Token(TK['IDENT'], line[start:i], lineno))
            continue

        # Unknown — skip
        i += 1


def tokenise(source, base_dir='.', include_dirs=None):
    """Tokenise full SNOBOL4 source with -INCLUDE expansion.
    Returns list of Token (NEWLINE-separated logical lines).
    """
    raw_lines = source.splitlines()
    logical_lines = []   # list of (joined_text, lineno)
    i = 0
    while i < len(raw_lines):
        line = raw_lines[i]
        lineno = i + 1

        # -INCLUDE directive
        m = re.match(r'^-INCLUDE\s+[\'"](.+?)[\'"]\s*$', line, re.IGNORECASE)
        if m:
            inc_name = m.group(1)
            search_dirs = [base_dir] + (include_dirs or [])
            inc_file = None
            for d in search_dirs:
                candidate = os.path.join(d, inc_name)
                if os.path.exists(candidate):
                    inc_file = candidate
                    break
            if inc_file:
                inc_src = open(inc_file).read()
                inc_base = os.path.dirname(inc_file)
            else:
                inc_src = ''
                inc_base = base_dir
            inc_tokens_sub = tokenise(inc_src, inc_base, include_dirs)
            # Recursion returns a flat token list; just extend
            logical_lines.append(('__INCLUDE__', lineno, inc_tokens_sub))
            i += 1
            continue

        # Skip comment lines (start with *)
        stripped = line.strip()
        if stripped.startswith('*') and not stripped.startswith('**'):
            i += 1
            continue

        # Skip blank lines
        if not stripped:
            i += 1
            continue

        # Continuation: lines starting with + or . in col 0 belong to previous
        if line and line[0] in ('+', '.'):
            # Append to previous logical line (strip the + or . prefix)
            if logical_lines and logical_lines[-1][0] != '__INCLUDE__':
                prev_text, prev_lineno = logical_lines[-1][:2]
                logical_lines[-1] = (prev_text + ' ' + line[1:].strip(), prev_lineno)
            i += 1
            continue

        logical_lines.append((line, lineno))
        i += 1

    # Now tokenise each logical line
    all_tokens = []
    for entry in logical_lines:
        if entry[0] == '__INCLUDE__':
            # Strip the trailing EOF sentinel from the included token list
            inc_toks = [t for t in entry[2] if t.kind != 'EOF']
            all_tokens.extend(inc_toks)
        else:
            text, lineno = entry
            line_toks = []
            _tokenise_line(text, lineno, line_toks)
            # If the original line starts with whitespace, prepend a SPACE sentinel
            # so the statement parser knows the line was indented (no label possible).
            if text and text[0] in (' ', '\t'):
                line_toks.insert(0, Token('SPACE', ' ', lineno))
            else:
                # Extract raw label from source (first whitespace-delimited token).
                # This preserves special chars like pp_! pp_:() that the lexer strips.
                import re as _re
                m = _re.match(r'^(\S+)', text)
                raw_label = m.group(1) if m else ''
                # Only inject a RAW_LABEL pseudo-token if it actually looks like a label
                # (not a keyword, not starting with quote/paren/digit/operator)
                if (raw_label and
                    raw_label[0] not in ('"', "'", '(', ')', '$', '&', '*', '-', '+', '=', ':', '0','1','2','3','4','5','6','7','8','9') and
                    raw_label not in ('DEFINE','DATA','ARRAY','TABLE','INPUT','OUTPUT','OPSYN','LOAD','UNLOAD','EVAL','CODE')):
                    line_toks.insert(0, Token('RAW_LABEL', raw_label, lineno))
            # Trim trailing SPACE only
            while line_toks and line_toks[-1].kind == 'SPACE':
                line_toks.pop()
            if line_toks:
                all_tokens.extend(line_toks)
                all_tokens.append(Token(TK['NEWLINE'], '\n', lineno))

    all_tokens.append(Token(TK['EOF'], '', 0))
    return all_tokens


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

class ParseError(Exception):
    def __init__(self, msg, lineno=0):
        super().__init__(f"Line {lineno}: {msg}")
        self.lineno = lineno


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos    = 0

    # --- token access ---

    def peek(self, offset=0):
        p = self.pos + offset
        if p >= len(self.tokens):
            return self.tokens[-1]   # EOF
        return self.tokens[p]

    def peek_kind(self, offset=0):
        return self.peek(offset).kind

    def at(self, *kinds):
        return self.peek().kind in kinds

    def consume(self, *kinds):
        t = self.peek()
        if kinds and t.kind not in kinds:
            raise ParseError(f"Expected {kinds}, got {t.kind} ({t.val!r})", t.line)
        self.pos += 1
        return t

    def try_consume(self, *kinds):
        if self.peek().kind in kinds:
            return self.consume()
        return None

    def skip_space(self):
        while self.peek().kind == 'SPACE':
            self.pos += 1

    def skip_newlines(self):
        # Skip only NEWLINEs — SPACE tokens at line start encode indentation info
        while self.peek().kind == 'NEWLINE':
            self.pos += 1

    def lineno(self):
        return self.peek().line

    # --- program ---

    def parse_program(self):
        stmts = []
        self.skip_newlines()
        while not self.at('EOF'):
            stmt = self.parse_statement()
            if stmt is not None:
                stmts.append(stmt)
            self.skip_newlines()
        return Program(stmts=stmts)

    # --- statement ---
    # Structure: [label]  [subject [pattern] [= replacement]]  [: goto]

    def parse_statement(self):
        lineno = self.lineno()

        # Collect all tokens up to the next NEWLINE
        toks = []
        while not self.at('NEWLINE', 'EOF'):
            toks.append(self.consume())
        self.try_consume('NEWLINE')

        if not toks:
            return None

        # Delegate to a sub-parser that works on the token slice
        return _parse_stmt_tokens(toks, lineno)


# ---------------------------------------------------------------------------
# Statement token-slice parser
# Operates on a flat list of tokens representing one logical statement.
# ---------------------------------------------------------------------------

def _parse_stmt_tokens(toks, lineno):
    """Parse a single logical statement from its token list."""
    # Track whether the statement was indented (first token was SPACE).
    # If indented → no label is possible (label must be in column 1).
    was_indented = toks and toks[0].kind == 'SPACE'
    # Extract raw label if present (preserves special chars like pp_!)
    raw_label_override = None
    if toks and toks[0].kind == 'RAW_LABEL':
        raw_label_override = toks[0].val
        toks = toks[1:]
    # Remove SPACE tokens — we will handle spacing contextually
    toks = [t for t in toks if t.kind != 'SPACE']
    if not toks:
        return None

    n = len(toks)
    i = [0]   # mutable index (boxed so inner functions can update it)

    def peek(off=0):
        p = i[0] + off
        return toks[p] if p < n else Token('EOF', '', lineno)

    def peek_kind(off=0):
        return peek(off).kind

    def at(*kinds, off=0):
        return peek(off).kind in kinds

    def consume(*kinds):
        t = peek()
        if kinds and t.kind not in kinds:
            raise ParseError(f"Expected {kinds}, got {t.kind} ({t.val!r})", lineno)
        i[0] += 1
        return t

    def try_consume(*kinds):
        if peek().kind in kinds:
            return consume()
        return None

    def remaining():
        return toks[i[0]:]

    # ----------------------------------------------------------------
    # Find the label: an IDENT starting at column 1 (token[0]).
    # A label is any IDENT in position 0 that is NOT followed by '('
    # (which would make it a function call subject).
    # Heuristic: if toks[0] is IDENT and the statement isn't just
    # a pattern-like expression starting at col 1, treat as label.
    # In SNOBOL4, labels are in the first 8 columns (col 1 = flush left).
    # We use a simpler rule: if the source token at pos 0 has col==1 or
    # the line starts without leading whitespace → it's a label.
    # Since we've stripped whitespace from the token stream, we use
    # positional heuristics: the original source line's first char.
    # For now: if toks[0] is IDENT and toks[1] is not '(' and the
    # identifier is not a known SNOBOL4 keyword that begins expressions,
    # treat it as a label.
    # ----------------------------------------------------------------

    label = None
    KEYWORDS_NOT_LABELS = {
        'DEFINE', 'DATA', 'ARRAY', 'TABLE', 'INPUT', 'OUTPUT',
        'OPSYN', 'LOAD', 'UNLOAD', 'EVAL', 'CODE',
    }

    # A label is an IDENT at position 0 that is NOT followed by '=' directly
    # (which would be subject=replacement with no pattern), unless the IDENT
    # is the subject.  We peek ahead to determine if this looks like:
    #   LABEL  subject  pattern ...  or
    #   subject = replacement
    # Heuristic: if toks[0] is IDENT and there is more content after it,
    # AND toks[1] is not '=', ':', NEWLINE, EOF → it's a label.
    # Edge case: "LABEL" alone on a line with no subject → label only.

    if (not was_indented                               # must be column 1
            and toks[0].kind == 'IDENT'
            and toks[0].val not in KEYWORDS_NOT_LABELS
            and not (n > 1 and toks[1].kind == 'EQ')      # subject = repl, no label
            and not (n > 1 and toks[1].kind == 'COLON')   # subject: goto? rare
            and not (n > 1 and toks[1].kind in ('LPAREN', 'LBRACKET'))  # call/subscript
            ):
        # Looks like a label if the next significant token is also an IDENT,
        # a string/int, '$', '&', or '*', OR if this is the only token (bare label).
        if n == 1:
            label = raw_label_override or toks[0].val
            return Stmt(label=label, lineno=lineno)
        next_kind = toks[1].kind
        if next_kind in ('IDENT', 'STR', 'INT', 'REAL', 'DOLLAR', 'AMP', 'STAR',
                         'LPAREN', 'MINUS'):
            label = consume().val
            # Override with raw label if available (preserves pp_! pp_:() etc)
            if raw_label_override and raw_label_override.startswith(label):
                label = raw_label_override

    # ----------------------------------------------------------------
    # Now parse: [subject [pattern] [= replacement]] [: goto]
    # ----------------------------------------------------------------

    # Find ':' that starts the goto field.
    # Walk backwards from end to find the last top-level ':' COLON.
    # A colon that is part of an ARRAY('lo:hi') spec will be inside parens.

    goto_idx = _find_goto_colon(toks, i[0])

    # Slice: body tokens are i[0]..goto_idx, goto tokens are goto_idx+1..end
    body_toks  = toks[i[0]:goto_idx] if goto_idx is not None else toks[i[0]:]
    goto_toks  = toks[goto_idx+1:]   if goto_idx is not None else []

    # Parse goto field
    goto = _parse_goto(goto_toks) if goto_toks else None

    # Parse body: [subject [pattern] [= replacement]]
    subject, pattern, replacement = _parse_body(body_toks, lineno)

    if label is None and subject is None and pattern is None and replacement is None and goto is None:
        return None

    return Stmt(label=label, subject=subject, pattern=pattern,
                replacement=replacement, goto=goto, lineno=lineno)


def _find_goto_colon(toks, start):
    """Find the index of the top-level ':' that starts the goto field.
    Returns index or None.
    """
    depth = 0
    for k in range(start, len(toks)):
        t = toks[k]
        if t.kind in ('LPAREN', 'LBRACKET'):
            depth += 1
        elif t.kind in ('RPAREN', 'RBRACKET'):
            depth -= 1
        elif t.kind == 'COLON' and depth == 0:
            return k
    return None


def _parse_goto(toks):
    """Parse goto tokens: S(label), F(label), (label) in any order."""
    if not toks:
        return None
    on_success = None
    on_failure = None
    unconditional = None
    i = 0
    while i < len(toks):
        t = toks[i]
        if t.kind == 'IDENT' and t.val.upper() == 'S':
            # S(label)
            i += 1
            if i < len(toks) and toks[i].kind == 'LPAREN':
                i += 1
                if i < len(toks) and toks[i].kind == 'IDENT':
                    on_success = toks[i].val
                    i += 1
                if i < len(toks) and toks[i].kind == 'RPAREN':
                    i += 1
        elif t.kind == 'IDENT' and t.val.upper() == 'F':
            # F(label)
            i += 1
            if i < len(toks) and toks[i].kind == 'LPAREN':
                i += 1
                if i < len(toks) and toks[i].kind == 'IDENT':
                    on_failure = toks[i].val
                    i += 1
                if i < len(toks) and toks[i].kind == 'RPAREN':
                    i += 1
        elif t.kind == 'LPAREN':
            # (label) — unconditional
            i += 1
            if i < len(toks) and toks[i].kind == 'IDENT':
                unconditional = toks[i].val
                i += 1
            if i < len(toks) and toks[i].kind == 'RPAREN':
                i += 1
        else:
            i += 1
    return Goto(on_success=on_success, on_failure=on_failure,
                unconditional=unconditional)


def _parse_body(toks, lineno):
    """Parse the body of a statement: [subject [pattern] [= replacement]].
    Returns (subject_expr, pattern_expr, replacement_expr), any may be None.
    """
    if not toks:
        return None, None, None

    # Find top-level '=' that separates subject+pattern from replacement
    eq_idx = None
    depth = 0
    for k, t in enumerate(toks):
        if t.kind in ('LPAREN', 'LBRACKET'):
            depth += 1
        elif t.kind in ('RPAREN', 'RBRACKET'):
            depth -= 1
        elif t.kind == 'EQ' and depth == 0:
            eq_idx = k
            break

    if eq_idx is not None:
        lhs_toks = toks[:eq_idx]
        rhs_toks = toks[eq_idx+1:]
    else:
        lhs_toks = toks
        rhs_toks = []

    replacement = None
    if rhs_toks:
        replacement = _parse_expr(rhs_toks, lineno)

    # LHS: subject [pattern]
    # Heuristic: if lhs_toks is a single IDENT (or subscripted var) followed by
    # pattern tokens, split at the boundary.
    # More precisely: parse the first "value expression" worth of tokens as subject,
    # remainder as pattern.

    subject = None
    pattern = None

    if lhs_toks:
        # Try to split subject from pattern.
        # Subject is the first atom/call/subscript before any pattern operator.
        subj_len = _subject_length(lhs_toks)
        subj_toks = lhs_toks[:subj_len]
        pat_toks  = lhs_toks[subj_len:]

        if subj_toks:
            subject = _parse_expr(subj_toks, lineno)
        if pat_toks:
            pattern = _parse_pattern(pat_toks, lineno)

    return subject, pattern, replacement


def _subject_length(toks):
    """Determine how many tokens belong to the subject expression.
    Subject is a value expression (variable, subscript, call) before
    the pattern starts.
    Returns the number of subject tokens (0 if entire thing is a pattern).
    """
    if not toks:
        return 0

    # If first token is a string literal or deferred ref '*IDENT', it's a
    # pattern, not a subject → subject length = 0
    if toks[0].kind == 'STR':
        return 0
    if toks[0].kind == 'STAR':
        return 0

    #  — indirect variable subject (e.g. $'#N')
    if toks[0].kind == 'DOLLAR':
        # Consume $ + one primary (STR or IDENT or '(' expr ')')
        k = 1
        if k < len(toks) and toks[k].kind in ('STR', 'IDENT'):
            k += 1
        elif k < len(toks) and toks[k].kind == 'LPAREN':
            depth = 1; k += 1
            while k < len(toks) and depth > 0:
                if toks[k].kind in ('LPAREN',): depth += 1
                elif toks[k].kind in ('RPAREN',): depth -= 1
                k += 1
        return k

    # &KEYWORD subject
    if toks[0].kind == 'AMP':
        return 2 if len(toks) > 1 and toks[1].kind == 'IDENT' else 1

    # IDENT possibly followed by '(' args ')' or '[' subscript ']'
    if toks[0].kind != 'IDENT':
        return 0

    # One IDENT — check what follows
    k = 1
    # Consume subscript chains: IDENT( ... ) or IDENT[ ... ]
    while k < len(toks):
        if toks[k].kind in ('LPAREN', 'LBRACKET'):
            # Skip balanced parens/brackets
            depth = 1
            k += 1
            while k < len(toks) and depth > 0:
                if toks[k].kind in ('LPAREN', 'LBRACKET'):
                    depth += 1
                elif toks[k].kind in ('RPAREN', 'RBRACKET'):
                    depth -= 1
                k += 1
        else:
            break

    # If k==1 and next token is a pattern operator or string → subject is IDENT
    # If k>1 → subject consumed IDENT + subscript/call
    # Special: if the statement is JUST one IDENT and nothing else, it's the subject
    return k


# ---------------------------------------------------------------------------
# Expression parser (value expressions)
# ---------------------------------------------------------------------------

class _ExprParser:
    def __init__(self, toks, lineno):
        self.toks   = toks
        self.lineno = lineno
        self.i      = 0

    def n(self):   return len(self.toks)
    def eof(self): return self.i >= len(self.toks)

    def peek(self, off=0):
        p = self.i + off
        return self.toks[p] if p < len(self.toks) else Token('EOF','',self.lineno)

    def at(self, *kinds, off=0):
        return self.peek(off).kind in kinds

    def consume(self, *kinds):
        t = self.peek()
        if kinds and t.kind not in kinds:
            raise ParseError(f"Expected {kinds}, got {t.kind} ({t.val!r})", self.lineno)
        self.i += 1
        return t

    def try_consume(self, *kinds):
        if self.peek().kind in kinds:
            return self.consume()
        return None

    def parse(self):
        e = self.parse_alt()
        return e

    def parse_alt(self):
        """Alternation: a | b (pattern alternation, lowest precedence in SNOBOL4)."""
        left = self.parse_concat()
        while self.at('PIPE'):
            self.consume()
            right = self.parse_concat()
            left = Expr(kind='alt', left=left, right=right)
        return left

    def parse_concat(self):
        """Concatenation: sequence of terms juxtaposed (no operator needed).
        In SNOBOL4, value concatenation is blank-separated — we handle this by
        treating two adjacent primary expressions as concatenation.
        We use SPACE tokens (were stripped) so just parse multiple terms."""
        left = self.parse_additive()
        # Pattern capture operators: . (conditional) and $ (immediate)
        while self.at('DOT', 'DOLLAR'):
            op = self.consume().kind
            var = self._parse_var_expr() if hasattr(self, '_parse_var_expr') else self.parse_additive()
            from ir import PatExpr
            kind = 'assign_cond' if op == 'DOT' else 'assign_imm'
            left = PatExpr(kind=kind, child=left, var=var)
        # Concatenation: if more tokens remain and they form a value expr, concat
        while not self.eof() and self._starts_primary():
            right = self.parse_additive()
            left = Expr(kind='concat', left=left, right=right)
            # Check for capture after each piece
            while self.at('DOT', 'DOLLAR'):
                op = self.consume().kind
                var = self._parse_var_expr() if hasattr(self, '_parse_var_expr') else self.parse_additive()
                from ir import PatExpr
                kind = 'assign_cond' if op == 'DOT' else 'assign_imm'
                left = PatExpr(kind=kind, child=left, var=var)
        return left

    def parse_additive(self):
        left = self.parse_multiplicative()
        while self.at('PLUS', 'MINUS'):
            op = self.consume().kind
            right = self.parse_multiplicative()
            kind = 'add' if op == 'PLUS' else 'sub'
            left = Expr(kind=kind, left=left, right=right)
        return left

    def parse_multiplicative(self):
        left = self.parse_power()
        while self.at('STAR', 'SLASH'):
            # Careful: STAR is also deferred ref in pattern context
            # In value expr context, * is multiply
            op = self.consume().kind
            right = self.parse_power()
            kind = 'mul' if op == 'STAR' else 'div'
            left = Expr(kind=kind, left=left, right=right)
        return left

    def parse_power(self):
        left = self.parse_unary()
        if self.at('STARSTAR'):
            self.consume()
            right = self.parse_unary()
            return Expr(kind='pow', left=left, right=right)
        return left

    def parse_unary(self):
        if self.at('MINUS'):
            self.consume()
            child = self.parse_primary()
            return Expr(kind='neg', child=child)
        return self.parse_primary()

    def _starts_primary(self):
        k = self.peek().kind
        return k in ('STR', 'INT', 'REAL', 'IDENT', 'DOLLAR', 'AMP', 'LPAREN')

    def parse_primary(self):
        t = self.peek()

        # String literal
        if t.kind == 'STR':
            self.consume()
            return Expr(kind='str', val=t.val)

        # Integer
        if t.kind == 'INT':
            self.consume()
            return Expr(kind='int', val=t.val)

        # Real
        if t.kind == 'REAL':
            self.consume()
            return Expr(kind='real', val=t.val)

        # $expr — indirect variable
        if t.kind == 'DOLLAR':
            self.consume()
            child = self.parse_primary()
            return Expr(kind='indirect', child=child)

        # &IDENT = keyword; & non-IDENT = concatenation operator
        if t.kind == 'AMP':
            self.consume()
            if not self.eof() and self.peek().kind == 'IDENT':
                name = self.consume().val
                return Expr(kind='keyword', val=name)
            # & as infix concat — right operand follows
            right = self.parse_primary()
            return right

        # Parenthesised expression
        if t.kind == 'LPAREN':
            self.consume()
            e = self.parse()
            self.try_consume('RPAREN')
            return e

        # IDENT — variable, function call, or subscript
        if t.kind == 'IDENT':
            name = self.consume().val

            # IDENT '(' args ')'  — function call OR subscript IDENT(i)
            if self.at('LPAREN'):
                self.consume()   # (
                args = self._parse_arglist()
                self.try_consume('RPAREN')
                # Could be followed by another '(' for 2D: ARRAY(i)(j) or func(x)(y)
                if self.at('LPAREN'):
                    self.consume()
                    args2 = self._parse_arglist()
                    self.try_consume('RPAREN')
                    # Treat as 2D subscript call
                    base = Expr(kind='call', name=name, args=args)
                    return Expr(kind='array', obj=base, subscripts=args2)
                return Expr(kind='call', name=name, args=args)

            # IDENT '[' subscripts ']'  — array subscript
            if self.at('LBRACKET'):
                self.consume()
                subs = self._parse_arglist()
                self.try_consume('RBRACKET')
                return Expr(kind='array', obj=Expr(kind='var', val=name), subscripts=subs)

            return Expr(kind='var', val=name)

        # Fallback — return null expr
        return Expr(kind='null')

    def _parse_arglist(self):
        """Parse comma-separated list of expressions (may be empty)."""
        args = []
        if self.eof() or self.at('RPAREN', 'RBRACKET'):
            return args
        args.append(self.parse())
        while self.at('COMMA'):
            self.consume()
            args.append(self.parse())
        return args


def _parse_expr(toks, lineno):
    """Parse a value expression from a token list."""
    toks = [t for t in toks if t.kind != 'SPACE']
    if not toks:
        return Expr(kind='null')
    p = _ExprParser(toks, lineno)
    return p.parse()


# ---------------------------------------------------------------------------
# Pattern parser
# ---------------------------------------------------------------------------

class _PatParser:
    def __init__(self, toks, lineno):
        self.toks   = toks
        self.lineno = lineno
        self.i      = 0

    def eof(self): return self.i >= len(self.toks)

    def peek(self, off=0):
        p = self.i + off
        return self.toks[p] if p < len(self.toks) else Token('EOF','',self.lineno)

    def at(self, *kinds, off=0):
        return self.peek(off).kind in kinds

    def consume(self, *kinds):
        t = self.peek()
        if kinds and t.kind not in kinds:
            raise ParseError(f"Pat: Expected {kinds}, got {t.kind}", self.lineno)
        self.i += 1
        return t

    def try_consume(self, *kinds):
        if self.peek().kind in kinds:
            return self.consume()
        return None

    def parse(self):
        return self.parse_alt()

    def parse_alt(self):
        left = self.parse_cat()
        while self.at('PIPE'):
            self.consume()
            right = self.parse_cat()
            left = PatExpr(kind='alt', left=left, right=right)
        return left

    def parse_cat(self):
        """Concatenation of pattern atoms (juxtaposition)."""
        parts = [self.parse_capture()]
        while not self.eof() and self._starts_pat_atom():
            parts.append(self.parse_capture())
        if len(parts) == 1:
            return parts[0]
        result = parts[0]
        for p in parts[1:]:
            result = PatExpr(kind='cat', left=result, right=p)
        return result

    def parse_capture(self):
        """pat_atom ('$'|'.') expr  — capture assignment."""
        atom = self.parse_atom()
        if self.at('DOLLAR'):
            self.consume()
            var = self._parse_var_expr()
            return PatExpr(kind='assign_imm', child=atom, var=var)
        if self.at('DOT'):
            self.consume()
            var = self._parse_var_expr()
            return PatExpr(kind='assign_cond', child=atom, var=var)
        return atom

    def _parse_var_expr(self):
        """Parse capture target: a variable/indirect expression."""
        toks_remaining = self.toks[self.i:]
        # Consume one primary worth of tokens
        ep = _ExprParser(toks_remaining, self.lineno)
        e  = ep.parse_primary()
        self.i += ep.i
        return e

    def _starts_pat_atom(self):
        k = self.peek().kind
        # Pattern atoms start with: STR, IDENT, *, (, |
        # But not: =, :, ), ], $, . (those terminate the pattern)
        return k in ('STR', 'IDENT', 'STAR', 'LPAREN', 'AMP', 'INT', 'REAL')

    def parse_atom(self):
        t = self.peek()

        # *IDENT — deferred pattern reference
        if t.kind == 'STAR':
            self.consume()
            if self.at('IDENT'):
                name = self.consume().val
                return PatExpr(kind='ref', name=name)
            # * alone — pattern multiply? treat as epsilon
            return PatExpr(kind='epsilon')

        # String literal — literal pattern
        if t.kind == 'STR':
            self.consume()
            return PatExpr(kind='lit', val=t.val)

        # Integer/real — usually an argument, treat as literal
        if t.kind in ('INT', 'REAL'):
            self.consume()
            return PatExpr(kind='lit', val=str(t.val))

        # Parenthesised pattern
        if t.kind == 'LPAREN':
            self.consume()
            inner = self.parse()
            self.try_consume('RPAREN')
            return inner

        # &KEYWORD in pattern context — treat as value expr wrapped
        if t.kind == 'AMP':
            toks_rest = self.toks[self.i:]
            ep = _ExprParser(toks_rest, self.lineno)
            e  = ep.parse_primary()
            self.i += ep.i
            return PatExpr(kind='var', val=e)

        # IDENT — variable, builtin call, or pattern name
        if t.kind == 'IDENT':
            name = self.consume().val

            # IDENT '(' args ')' — builtin or user-defined pattern call
            if self.at('LPAREN'):
                self.consume()
                args = self._parse_pat_args()
                self.try_consume('RPAREN')
                return PatExpr(kind='call', name=name, args=args)

            # Bare IDENT — variable holding a pattern
            return PatExpr(kind='var', val=name)

        # Fallback epsilon
        return PatExpr(kind='epsilon')

    def _parse_pat_args(self):
        """Parse comma-separated pattern or expression args."""
        args = []
        if self.eof() or self.at('RPAREN'):
            return args
        args.append(self._parse_pat_or_expr())
        while self.at('COMMA'):
            self.consume()
            args.append(self._parse_pat_or_expr())
        return args

    def _parse_pat_or_expr(self):
        """Parse an argument that could be a pattern or expression."""
        # Heuristic: if it starts with a string or '*' IDENT, it's a pattern.
        # Otherwise treat as expression.
        t = self.peek()
        if t.kind in ('STR', 'STAR'):
            return self.parse()
        # Parse as value expression
        toks_rest = self.toks[self.i:]
        ep = _ExprParser(toks_rest, self.lineno)
        e  = ep.parse()
        self.i += ep.i
        return e


def _parse_pattern(toks, lineno):
    """Parse a pattern expression from a token list."""
    toks = [t for t in toks if t.kind != 'SPACE']
    if not toks:
        return None
    p = _PatParser(toks, lineno)
    return p.parse()


# ---------------------------------------------------------------------------
# Top-level entry point
# ---------------------------------------------------------------------------

def parse_file(path, include_dirs=None):
    """Parse a SNOBOL4 source file, returning a Program."""
    src      = open(path).read()
    base_dir = os.path.dirname(os.path.abspath(path))
    tokens   = tokenise(src, base_dir, include_dirs)
    parser   = Parser(tokens)
    return parser.parse_program()


def parse_source(src, base_dir='.', include_dirs=None):
    """Parse SNOBOL4 source string, returning a Program."""
    tokens = tokenise(src, base_dir, include_dirs)
    parser = Parser(tokens)
    return parser.parse_program()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: sno_parser.py <file.sno>", file=sys.stderr)
        sys.exit(1)

    prog = parse_file(sys.argv[1])
    print(f"Parsed {len(prog.stmts)} statements")
    for s in prog.stmts[:20]:
        label = f"{s.label}:" if s.label else "      "
        subj  = f" subj={s.subject.kind}" if s.subject else ""
        pat   = f" pat={s.pattern.kind}"  if s.pattern  else ""
        repl  = f" ={s.replacement.kind}" if s.replacement else ""
        goto  = f" :{s.goto}" if s.goto else ""
        print(f"  L{s.lineno:4d} {label}{subj}{pat}{repl}{goto}")
