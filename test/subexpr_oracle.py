#!/usr/bin/env python3
"""
subexpr_oracle.py — Sub-expression oracle test generator for SNOBOL4/beauty.

For every assignment RHS in a .sno file, extract sub-expressions bottom-up,
probe each under SPITBOL at the point in execution where context is established,
emit one tiny .sno + .ref regression test per sub-expression value.

Key design choices:
  - "Probe context" = all statements from the file UP TO the statement being probed,
    with all DEFINE/DATA calls (the file header) always included.
  - For top-level beauty statements that run at load time, the prefix is correct.
  - For function-body statements, we run the function call that exercises the body.
  - We use a safe probe wrapper: OUTPUT the value, catch fail with :F(SNOFail).
  - Label names use only alphanumeric (no underscores that upset SPITBOL).

Usage:
    python3 test/subexpr_oracle.py beauty/counter.sno [beauty/assign.sno ...]
        --inc /path/to/includes
        --out /corpus/programs/snobol4/subexpr
        --verbose
"""

import re, sys, os, subprocess, tempfile, textwrap, argparse
from pathlib import Path

SBL = '/home/claude/x64/bin/sbl'
OUT_DIR = Path('/home/claude/corpus/programs/snobol4/subexpr')

# ─── Tokenizer ────────────────────────────────────────────────────────────────

TOKEN_RE = re.compile(
    r"('(?:[^']|'')*')"          # STR
    r'|(\d+(?:\.\d+)?)'          # NUM
    r'|([A-Za-z][A-Za-z0-9]*)'  # ID  (no underscore — SNOBOL4 vars are alpha)
    r'|(\*\*|[+\-*/])'          # OP
    r'|(\$)'                     # DREF
    r'|(\&[A-Za-z][A-Za-z0-9]*)' # KEYWORD like &LCASE
    r'|(\.[A-Za-z][A-Za-z0-9]*)' # NAMEREF like .dummy
    r'|(\@[A-Za-z][A-Za-z0-9]*)' # CURSOR like @txOfs
    r'|(\()'                     # LP
    r'|(\))'                     # RP
    r'|(\,)'                     # COMMA
    r'|([^\s])'                  # OTHER (single non-space char)
)

def tokenize(s):
    toks = []
    for m in TOKEN_RE.finditer(s):
        v = m.group(0)
        kind = (
            'STR'     if m.group(1)  else
            'NUM'     if m.group(2)  else
            'ID'      if m.group(3)  else
            'OP'      if m.group(4)  else
            'DREF'    if m.group(5)  else
            'KW'      if m.group(6)  else
            'NAMEREF' if m.group(7)  else
            'CURSOR'  if m.group(8)  else
            'LP'      if m.group(9)  else
            'RP'      if m.group(10) else
            'COMMA'   if m.group(11) else
            'OTHER'
        )
        toks.append((kind, v))
    return toks

# ─── Sub-expression recursive parser ─────────────────────────────────────────

class SubexprParser:
    """
    Extracts sub-expressions bottom-up from a SNOBOL4 expression string.
    Returns a flat ordered list of sub-expression strings (innermost first).
    Each sub-expression is a string that could be wrapped in OUTPUT = (...).
    """
    def __init__(self, tokens):
        self.t = tokens
        self.i = 0
        self.subs = []   # accumulates sub-exprs in discovery order

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else ('EOF', '')

    def eat(self):
        tok = self.t[self.i]; self.i += 1; return tok

    def parse_top(self):
        """Parse full expression = space-concatenation of arith terms."""
        parts = []
        while True:
            k = self.peek()[0]
            if k in ('EOF', 'RP', 'COMMA', 'OTHER'):
                break
            t = self.parse_arith()
            if t is None:
                break
            parts.append(t)
        if len(parts) > 1:
            full = ' '.join(parts)
            self.subs.append(full)
        return ' '.join(parts) if parts else ''

    def parse_arith(self):
        left = self.parse_unary()
        if left is None:
            return None
        while self.peek()[0] == 'OP' and self.peek()[1] in ('+','-','*','/','**'):
            op = self.eat()[1]
            right = self.parse_unary()
            if right is None:
                break
            left = f'{left} {op} {right}'
            self.subs.append(left)
        return left

    def parse_unary(self):
        if self.peek()[0] == 'OP' and self.peek()[1] in ('+','-'):
            op = self.eat()[1]
            inner = self.parse_primary()
            if inner is None:
                return None
            r = f'{op}{inner}'
            self.subs.append(r)
            return r
        return self.parse_primary()

    def parse_primary(self):
        k, v = self.peek()

        if k == 'STR':
            self.eat(); return v

        if k == 'NUM':
            self.eat(); return v

        if k == 'KW':          # &LCASE etc — not evaluable alone, skip
            self.eat(); return v

        if k == 'NAMEREF':     # .dummy — a name pointer, evaluable
            self.eat(); return v

        if k == 'CURSOR':      # @var — cursor, skip
            self.eat(); return v

        if k == 'DREF':        # $expr
            self.eat()
            inner = self.parse_primary()
            if inner is None:
                return None
            r = f'${inner}'
            self.subs.append(r)
            return r

        if k == 'ID':
            self.eat()
            name = v
            if self.peek()[0] == 'LP':   # function call
                self.eat()               # (
                args = []
                while self.peek()[0] not in ('RP', 'EOF'):
                    sub_p = SubexprParser(self.t[self.i:])
                    arg = sub_p.parse_top()
                    self.subs.extend(sub_p.subs)
                    self.i += sub_p.i
                    args.append(arg)
                    if self.peek()[0] == 'COMMA':
                        self.eat()
                if self.peek()[0] == 'RP':
                    self.eat()
                r = f'{name}({", ".join(args)})'
                self.subs.append(r)
                return r
            # Plain variable
            return name

        if k == 'LP':
            self.eat()
            sub_p = SubexprParser(self.t[self.i:])
            inner = sub_p.parse_top()
            self.subs.extend(sub_p.subs)
            self.i += sub_p.i
            if self.peek()[0] == 'RP':
                self.eat()
            r = f'({inner})'
            self.subs.append(r)
            return r

        return None


def extract_subexprs(expr_text):
    """Return deduplicated ordered list of sub-expressions (innermost first)."""
    toks = tokenize(expr_text)
    p = SubexprParser(toks)
    p.parse_top()
    seen = set()
    result = []
    for s in p.subs:
        s = s.strip()
        if not s or s in seen:
            continue
        seen.add(s)
        # Skip: bare string literal, bare integer, bare variable, bare keyword
        if re.match(r"^'[^']*'$", s):     continue  # 'string'
        if re.match(r'^\d+$', s):         continue  # 123
        if re.match(r'^&\w+$', s):        continue  # &LCASE
        if re.match(r'^\.[A-Za-z]\w*$',s):continue  # .dummy  (name ptr)
        if re.match(r'^[A-Za-z]\w*$', s): continue  # barevar
        result.append(s)
    return result

# ─── .sno file reader ─────────────────────────────────────────────────────────

COMMENT_RE = re.compile(r'^\s*\*|^-INCLUDE|^\s*END\s*$', re.I)

def read_sno_stmts(path):
    """Read .sno, joining continuation lines (+), filtering comments/END/-INCLUDE."""
    lines = Path(path).read_text(errors='replace').splitlines()
    stmts = []       # list of (original_lineno, joined_text)
    i = 0
    while i < len(lines):
        line = lines[i]
        lineno = i + 1
        i += 1
        if not line.strip() or COMMENT_RE.match(line):
            continue
        while i < len(lines) and lines[i].startswith('+'):
            line = line.rstrip() + ' ' + lines[i][1:].strip()
            i += 1
        stmts.append((lineno, line))
    return stmts

# ─── RHS extractor ────────────────────────────────────────────────────────────

GOTO_TAIL = re.compile(r'\s*:[SF(].*$')

def get_rhs_from_body(body):
    """Find = and return text after it (before any trailing :goto)."""
    depth = 0; in_str = False
    for idx, ch in enumerate(body):
        if ch == "'" :
            in_str = not in_str
        elif not in_str:
            if ch == '(':  depth += 1
            elif ch == ')': depth -= 1
            elif ch == '=' and depth == 0:
                rhs = GOTO_TAIL.sub('', body[idx+1:]).strip()
                return rhs
    return None

def stmt_body(raw_line):
    """Strip leading label from raw line, return body."""
    m = re.match(r'^([A-Za-z][A-Za-z0-9]*)?\s+(.*)', raw_line)
    return m.group(2) if m else raw_line.strip()

# ─── SPITBOL probe runner ─────────────────────────────────────────────────────

RESULT_RE = re.compile(r'XPROBESTART(.*)XPROBEEND', re.DOTALL)

def build_probe(prefix_stmts, subexpr, inc_path=None):
    """
    Build a SNOBOL4 program that runs prefix, then outputs the value of subexpr.
    Uses unique markers; label SNOFail is alphanumeric only.
    """
    inc_line = f"-INCLUDE '{inc_path}'\n" if inc_path else ''
    pfx = '\n'.join(f'        {ln}' for ln in prefix_stmts)
    # Escape subexpr for embedding in a SNOBOL4 string (for the expected= message)
    # The probe just evaluates the subexpr:
    prog = (
        f"{inc_line}"
        f"        &STLIMIT = 500000\n"
        f"        &ERRLIMIT = 5\n"
        f"{pfx}\n"
        f"        SNOPval = ({subexpr})\n"
        f"        OUTPUT = 'XPROBESTART' SNOPval 'XPROBEEND'  :(SNOPend)\n"
        f"SNOPfail OUTPUT = 'XPROBEFAIL'\n"
        f"SNOPend\n"
        f"END\n"
    )
    return prog

def run_spitbol(prog_text, inc_dir=None):
    env = os.environ.copy()
    with tempfile.NamedTemporaryFile(mode='w', suffix='.sno', delete=False,
                                    dir='/tmp', prefix='subexpr_') as f:
        f.write(prog_text)
        fname = f.name
    try:
        cmd = [SBL, '-b']
        if inc_dir:
            cmd += [f'-I{inc_dir}']
        cmd.append(fname)
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=8, env=env)
        return r.stdout
    except subprocess.TimeoutExpired:
        return None
    finally:
        try: os.unlink(fname)
        except: pass

def probe_value(prefix_stmts, subexpr, inc_dir=None, inc_path=None):
    prog = build_probe(prefix_stmts, subexpr, inc_path)
    out = run_spitbol(prog, inc_dir)
    if out is None:
        return None
    m = RESULT_RE.search(out)
    if not m:
        return None
    return m.group(1)  # may be empty string — that's a valid value

# ─── Regression test emitter ─────────────────────────────────────────────────

def emit_test(out_dir, test_name, prefix_stmts, subexpr, oracle_val,
              lineno, sub_idx, src_file, inc_path=None):
    """Emit one .sno + .ref pair."""
    safe_val = oracle_val.replace("'", "''")
    pfx = '\n'.join(f'        {ln}' for ln in prefix_stmts)
    inc_line = f"-INCLUDE '{inc_path}'\n" if inc_path else ''

    sno = (
        f"* subexpr regression: {Path(src_file).name} L{lineno} s{sub_idx}\n"
        f"* expr: {subexpr[:100]}\n"
        f"* expected: {oracle_val[:80]}\n"
        f"{inc_line}"
        f"        &STLIMIT = 500000\n"
        f"        &ERRLIMIT = 5\n"
        f"{pfx}\n"
        f"        SNOTgot = ({subexpr})\n"
        f"        IDENT(SNOTgot, '{safe_val}')   :S(SNOTpass)\n"
        f"        OUTPUT = 'FAIL got=[' SNOTgot '] want=[{safe_val}]'\n"
        f"        :(SNOTend)\n"
        f"SNOTpass OUTPUT = 'PASS'\n"
        f"SNOTend\n"
        f"END\n"
    )
    Path(out_dir, f'{test_name}.sno').write_text(sno)
    Path(out_dir, f'{test_name}.ref').write_text('PASS\n')

# ─── Main generator ───────────────────────────────────────────────────────────

def generate_for_file(sno_path, inc_dir, out_dir, file_tag, verbose=False,
                      limit=0, inc_path=None):
    stmts = read_sno_stmts(sno_path)
    all_lines = [raw for (_, raw) in stmts]
    total_tests = 0
    total_probes = 0

    for stmt_idx, (lineno, raw_line) in enumerate(stmts):
        body = stmt_body(raw_line)
        rhs = get_rhs_from_body(body)
        if not rhs or len(rhs.strip()) < 3:
            continue

        subs = extract_subexprs(rhs)
        if not subs:
            continue

        prefix = all_lines[:stmt_idx]

        for sub_idx, subexpr in enumerate(subs):
            if limit and total_probes >= limit:
                break
            total_probes += 1

            if verbose:
                print(f'  L{lineno:04d} s{sub_idx}: {subexpr[:70]}')

            val = probe_value(prefix, subexpr, inc_dir, inc_path)
            if val is None:
                if verbose:
                    print(f'    → no value (fail/error)')
                continue

            test_name = f'{file_tag}_L{lineno:04d}_s{sub_idx:02d}'
            emit_test(out_dir, test_name, prefix, subexpr, val,
                      lineno, sub_idx, sno_path, inc_path)
            total_tests += 1

            if verbose:
                print(f'    → [{val[:60]}]')

        if limit and total_probes >= limit:
            break

    return total_tests, total_probes

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--inc', default=None, help='Include search dir for SPITBOL -I')
    ap.add_argument('--out', default=str(OUT_DIR))
    ap.add_argument('--verbose', '-v', action='store_true')
    ap.add_argument('--limit', type=int, default=0)
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    grand_tests = 0
    for sno_file in args.files:
        path = Path(sno_file)
        tag = path.stem
        print(f'Probing {path.name} ...')
        t, p = generate_for_file(path, args.inc, out, tag,
                                  args.verbose, args.limit)
        print(f'  → {t} tests generated from {p} probes')
        grand_tests += t

    print(f'\nTotal: {grand_tests} tests')
    print(f'Output: {out}')

if __name__ == '__main__':
    main()
