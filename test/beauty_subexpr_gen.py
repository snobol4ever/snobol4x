#!/usr/bin/env python3
"""
beauty_subexpr_gen.py — Sub-expression oracle test generator.

Two-run protocol:
  Run 1 (oracle): run driver to &STLIMIT=N-1, &DUMP=2 captures state,
                  gauntlet OUTPUTs each sub-expression inside-out.
  Run 2 (snippet): isolated .sno — state block from DUMP scalars,
                   assert block (IDENT per sub-expr). No driver, no includes.

See GOAL-SUBEXPR-ORACLE.md for full design.
"""

import re, sys, os, subprocess, tempfile, argparse, random
from pathlib import Path

SBL   = '/home/claude/x64/bin/sbl'
SCRIP = '/home/claude/one4all/scrip'
BEAUTY = Path('/home/claude/corpus/programs/snobol4/beauty')
OUT    = Path('/home/claude/corpus/programs/snobol4/subexpr')

# ── Tokenizer ─────────────────────────────────────────────────────────────────

TOKEN_RE = re.compile(
    r"('(?:[^']|'')*')"             # g1  STR
    r'|(\d+(?:\.\d+)?)'            # g2  NUM
    r'|([A-Za-z][A-Za-z0-9]*)'    # g3  ID
    r'|(\*\*)'                     # g4  STARSTAR
    r'|(\*)'                       # g5  STAR  (*expr immediate value)
    r'|([+\-/])'                   # g6  ARITH
    r'|(&[A-Za-z][A-Za-z0-9]*)'  # g7  KW
    r'|(\$)'                       # g8  DREF
    r"|(\.'(?:[^']|'')*')"         # g9  NAMELIT  .'$B'
    r'|(\.[A-Za-z][A-Za-z0-9]*)' # g10 NAMEREF  .dummy
    r'|(@[A-Za-z][A-Za-z0-9]*)' # g11 CURSOR   @txOfs
    r'|(\|)'                       # g12 ALT
    r'|(\()'                       # g13 LP
    r'|(\))'                       # g14 RP
    r'|(\,)'                       # g15 COMMA
    r'|([^\s])'                    # g16 OTHER
)
GMAP = {1:'STR',2:'NUM',3:'ID',4:'STARSTAR',5:'STAR',6:'ARITH',
        7:'KW',8:'DREF',9:'NAMELIT',10:'NAMEREF',11:'CURSOR',
        12:'ALT',13:'LP',14:'RP',15:'COMMA',16:'OTHER'}

def tokenize(s):
    return [(GMAP[m.lastindex], m.group(0)) for m in TOKEN_RE.finditer(s)]

# ── Sub-expression extractor ──────────────────────────────────────────────────

class SubP:
    """Recursive descent — collects sub-expressions bottom-up in self.subs."""
    def __init__(self, toks):
        self.t = toks; self.i = 0; self.subs = []

    def peek(self): return self.t[self.i] if self.i < len(self.t) else ('EOF','')
    def eat(self):  r = self.t[self.i]; self.i += 1; return r
    def done(self): return self.i >= len(self.t)
    def stop(self):
        k,v = self.peek()
        return k in ('EOF','RP','COMMA') or v in (':',';')

    def parse_top(self): return self._alt()

    def _alt(self):
        left = self._concat()
        while not self.done() and self.peek()[0] == 'ALT':
            self.eat(); right = self._concat()
            left = f'{left} | {right}'; self.subs.append(left)
        return left

    def _concat(self):
        # Space-concat and pattern ops (. and $) at same level
        parts = []
        while not self.stop():
            k,v = self.peek()
            # pattern conditional assign:  left . target
            if v == '.' and k == 'OTHER':
                self.eat(); tgt = self._primary()
                base = ' '.join(parts) if parts else ''
                expr = f'{base} . {tgt}' if base else f'. {tgt}'
                self.subs.append(expr); parts = [expr]; continue
            # immediate assign / cursor:  left $ target  ($ after a term)
            if k == 'DREF' and parts:
                self.eat(); tgt = self._primary()
                base = ' '.join(parts)
                expr = f'{base} $ {tgt}'
                self.subs.append(expr); parts = [expr]; continue
            t = self._arith()
            if t is None: break
            parts.append(t)
        if len(parts) > 1: self.subs.append(' '.join(parts))
        return ' '.join(parts) if parts else ''

    def _arith(self):
        left = self._unary()
        if not left: return None
        while not self.done() and self.peek()[0] in ('ARITH','STARSTAR'):
            op = self.eat()[1]; right = self._unary()
            if not right: break
            left = f'{left} {op} {right}'; self.subs.append(left)
        return left

    def _unary(self):
        k,v = self.peek()
        if k == 'ARITH' and v in ('+','-'):
            self.eat(); inner = self._primary()
            if not inner: return None
            r = f'{v}{inner}'; self.subs.append(r); return r
        if k == 'STAR':
            self.eat(); inner = self._primary()
            if not inner: return None
            r = f'*{inner}'; self.subs.append(r); return r
        return self._primary()

    def _primary(self):
        k,v = self.peek()
        if k in ('STR','NUM','KW'): self.eat(); return v
        if k in ('NAMEREF','NAMELIT'): self.eat(); return v
        if k == 'CURSOR': self.eat(); return v
        if k == 'DREF':
            self.eat(); inner = self._primary()
            if not inner: return None
            r = f'${inner}'; self.subs.append(r); return r
        if k == 'ID':
            self.eat(); name = v
            if not self.done() and self.peek()[0] == 'LP':
                self.eat(); args = []
                while not self.done() and self.peek()[0] != 'RP':
                    sp2 = SubP(self.t[self.i:])
                    arg = sp2.parse_top()
                    self.subs.extend(sp2.subs); self.i += sp2.i
                    args.append(arg)
                    if not self.done() and self.peek()[0] == 'COMMA': self.eat()
                if not self.done() and self.peek()[0] == 'RP': self.eat()
                r = f'{name}({", ".join(args)})'; self.subs.append(r); return r
            return name
        if k == 'LP':
            self.eat(); sp2 = SubP(self.t[self.i:])
            inner = sp2.parse_top(); self.subs.extend(sp2.subs); self.i += sp2.i
            if not self.done() and self.peek()[0] == 'RP': self.eat()
            r = f'({inner})'; self.subs.append(r); return r
        return None

def extract_subexprs(text):
    """All sub-expressions from expression text, innermost first, deduped."""
    toks = tokenize(text)
    p = SubP(toks); p.parse_top()
    seen = set(); result = []
    for s in p.subs:
        s = s.strip()
        if not s or s in seen: continue
        seen.add(s)
        if re.match(r"^'[^']*'$", s): continue   # bare string
        if re.match(r'^\d+$', s):     continue   # bare int
        if re.match(r'^&\w+$', s):    continue   # bare keyword
        if re.match(r'^\.[A-Za-z]',s):continue   # bare nameref
        if re.match(r'^@[A-Za-z]',s): continue   # bare cursor
        if re.match(r'^[A-Za-z]\w*$',s): continue # bare var
        result.append(s)
    return result

def stmt_subexprs(raw_line):
    """Extract sub-expressions from all parts of a statement line."""
    # Strip label, strip :goto
    line = raw_line.strip()
    m = re.match(r'^[A-Za-z][A-Za-z0-9]*\s+(.*)', line)
    body = m.group(1) if m else line
    goto = re.search(r'\s*:[SF(]', body)
    if goto: body = body[:goto.start()]
    # Split on = to get lhs and rhs (respecting parens/strings)
    depth=0; in_str=False; eq=-1
    for i,ch in enumerate(body):
        if ch=="'": in_str=not in_str
        elif not in_str:
            if ch=='(': depth+=1
            elif ch==')': depth-=1
            elif ch=='=' and depth==0: eq=i; break
    parts = [body[:eq].strip(), body[eq+1:].strip()] if eq>=0 else [body.strip()]
    seen=set(); result=[]
    for part in parts:
        if not part: continue
        for s in extract_subexprs(part):
            if s not in seen: seen.add(s); result.append(s)
    return result

# ── SPITBOL runner ────────────────────────────────────────────────────────────

def run_sbl(prog_text, timeout=20):
    with tempfile.NamedTemporaryFile(mode='w', suffix='.sno', delete=False,
                                     dir='/tmp', prefix='bsg_') as f:
        f.write(prog_text); fname = f.name
    try:
        r = subprocess.run([SBL, '-b', fname], capture_output=True, timeout=timeout)
        return r.stdout.decode('utf-8', errors='replace') if r.stdout else ''
    except subprocess.TimeoutExpired:
        return None
    finally:
        try: os.unlink(fname)
        except: pass

def run_scrip(prog_text, timeout=15):
    with tempfile.NamedTemporaryFile(mode='w', suffix='.sno', delete=False,
                                     dir='/tmp', prefix='bsg_sc_') as f:
        f.write(prog_text); fname = f.name
    try:
        r = subprocess.run([SCRIP, '--ir-run', fname],
                           capture_output=True, timeout=timeout,
                           errors="replace",
                           cwd='/home/claude/one4all')
        return r.stdout.decode('utf-8', errors='replace')
    except subprocess.TimeoutExpired:
        return None
    finally:
        try: os.unlink(fname)
        except: pass

# ── Driver source builder ─────────────────────────────────────────────────────

def build_driver_src(driver_path, beauty_dir):
    """Expand includes to full paths, strip &STLIMIT, deduplicate global.sno."""
    beauty_dir = Path(beauty_dir)
    src = Path(driver_path).read_text(errors='replace')
    out = [f"-INCLUDE '{beauty_dir}/global.sno'"]
    seen = {str(beauty_dir / 'global.sno')}
    for line in src.splitlines():
        ls = line.strip()
        if re.match(r"^-INCLUDE\s+'global\.sno'", ls, re.I): continue
        if any(ci in ls for ci in CRASH_INCLUDES): continue
        if re.match(r'^-INCLUDE', ls, re.I):
            mm = re.search(r"'([^']+)'", line)
            if mm:
                inc = beauty_dir / mm.group(1)
                key = str(inc)
                if inc.exists() and key not in seen:
                    seen.add(key); out.append(f"-INCLUDE '{inc}'")
                continue
        if re.match(r'\s*&STLIMIT\s*=', ls, re.I): continue
        if re.match(r'^\s*END\s*$', line): continue
        out.append(line)
    return '\n'.join(out)

# ── DUMP parser ───────────────────────────────────────────────────────────────

DOLLAR_NAME_RE = re.compile(r'^\$[A-Za-z0-9_#@$]+\s*=\s*(.*)$')

def parse_dump(dump_text):
    """
    Parse SPITBOL &DUMP=2 output into {varname: raw_value} dict.
    Only scalar variables (string/integer) — skips DATA struct lines.
    """
    scalars = {}
    in_nat = False
    for line in dump_text.splitlines():
        if re.match(r'dump of natural variables', line, re.I):
            in_nat = True; continue
        if re.match(r'dump of keyword', line, re.I):
            in_nat = False; continue
        if not in_nat: continue
        line = line.strip()
        if not line: continue
        # Skip DATA struct object lines like "link_counter #1"
        if re.match(r'[A-Za-z][A-Za-z0-9_]* #\d+', line): continue
        # field(obj) = val lines -- skip (DATA struct fields)
        if re.match(r'[a-z]+\(', line): continue
        # Normal: varname = value
        m = re.match(r'^([A-Za-z#@$][A-Za-z0-9_#@$]*)\s*=\s*(.*)$', line)
        if m:
            name = m.group(1)
            val  = m.group(2).strip()
            scalars[name] = val
    return scalars

# Names whose DUMP values contain raw non-printable bytes — use CHAR(N)
NONPRINT_NAMES = {
    'nl':'CHAR(10)', 'lf':'CHAR(10)', 'cr':'CHAR(13)', 'bs':'CHAR(8)',
    'ht':'CHAR(9)',  'tab':'CHAR(9)', 'vt':'CHAR(11)', 'ff':'CHAR(12)',
    'nul':'CHAR(0)',
}
# Names whose values are large charset strings from global.sno — skip them
SKIP_NAMES = {'x0xxxxxxx','x1xxxxxxx','x10xxxxxx','x110xxxxx',
              'x1110xxxx','x11110xxx','x11111xxx','utf_array'}

# Includes that cause error 248 (OPSYN/FENCE redefinitions) in SPITBOL oracle
CRASH_INCLUDES = {'FENCE.sno', 'io.sno'}


def dump_val_to_snobol(name, raw_val):
    """
    Convert a DUMP value entry to a SNOBOL4 assignment statement.
    Returns None if we cannot reconstruct (DATA struct ref, charset string).
    """
    if name.lower() in SKIP_NAMES: return None
    if re.search(r'#\d+', raw_val): return None   # DATA struct ref
    # Known non-printable names
    if name.lower() in NONPRINT_NAMES:
        return f"        {name} = {NONPRINT_NAMES[name.lower()]}"
    # Integer
    if re.match(r'^-?\d+$', raw_val.strip()):
        return f"        {name} = {raw_val.strip()}"
    # String already quoted in dump
    if raw_val.startswith("'"):
        # Check for embedded non-printable bytes — skip if found
        inner = raw_val[1:raw_val.rfind("'")]
        for ch in inner:
            if ord(ch) < 32 and ch not in ("\t",): return None
        return f"        {name} = {raw_val}"
    # Empty
    if raw_val == '':
        return f"        {name} = ''"
    return None

# ── Run 1: Oracle gauntlet ────────────────────────────────────────────────────

MS = 'XGSSTART'; ME = 'XGSEND'

PROBE_HELPER = """        DEFINE('SNBprobe(SNBx)SNBdt,SNBvs')     :(SNBprobeEnd)
SNBprobe
        SNBdt = DATATYPE(SNBx)
        SNBvs = IDENT(SNBdt, 'pattern') 'PAT'
        SNBvs = IDENT(SNBdt, 'expression') 'EXPR'
        SNBvs = IDENT(SNBdt, 'code') 'CODE'
        SNBvs = DIFFER(SNBvs) SNBvs
        SNBvs = IDENT(SNBvs) SNBdt '|' SNBx
        OUTPUT = 'XGSSTART|' SNBdt '|' SNBvs '|XGSEND'
        SNBprobe = SNBdt                            :(NRETURN)
SNBprobeEnd"""

def build_gauntlet_lines(subexprs, label_base):
    """
    One SNBprobe() call per sub-expression.
    Output format: XGSSTART|type|value|XGSEND
    Pattern objects stringify as 'pattern' in the value slot — acceptable.
    """
    lines = [PROBE_HELPER]
    for sub in subexprs:
        lines.append(f"        SNBprobe({sub})")
    return lines

def run_oracle_gauntlet(driver_src, stlimit, subexprs, beauty_dir):
    """
    Run 1: driver to stlimit, then gauntlet.
    Returns (dump_scalars, expr_values) where expr_values = {sub: value_str}.
    """
    gauntlet = '\n'.join(build_gauntlet_lines(subexprs, 'G'))
    safe_driver = '\n'.join(
        line for line in driver_src.splitlines()
        if not any(ci in line for ci in CRASH_INCLUDES)
    )
    prog = (
        f"        &STLIMIT = {stlimit}\n"
        f"        &ERRLIMIT = 1\n"
        f"        &TRIM = 1\n"
        f"{safe_driver}\n"
        f"        &STLIMIT = 10000000\n"
        f"{gauntlet}\n"
        f"END\n"
    )
    out = run_sbl(prog)
    if out is None: return {}, {}

    # Parse DUMP
    scalars = parse_dump(out)

    # Parse gauntlet: one XGSSTART|type|value|XGSEND line per sub-expression
    # Match them in order of appearance
    # Format: XGSSTART|type|PAT|XGSEND  (pattern/expression/code)
    #      or XGSSTART|type|type|value|XGSEND  (string/integer/name)
    probe_re = re.compile(
        r'XGSSTART\|([^|]+)\|(.+?)\|XGSEND', re.MULTILINE)
    matches = probe_re.findall(out)
    expr_vals = {}
    for i, sub in enumerate(subexprs):
        if i >= len(matches): break
        typ, rest = matches[i]
        if typ in ('pattern', 'expression', 'code'):
            expr_vals[sub] = ('__TYPE__', typ)
        else:
            # rest = "type|value" — extract value after second |
            pipe = rest.find('|')
            val = rest[pipe+1:] if pipe >= 0 else rest
            expr_vals[sub] = ('__VALUE__', val)

    return scalars, expr_vals

# ── Run 2: Isolated snippet emitter ───────────────────────────────────────────

def make_alias(name):
    """Make a safe SNOBOL4 variable alias for indirect names like $B, #L, @S."""
    return 'SNB' + re.sub(r'[^A-Za-z0-9]', 'x', name)


ASSERT_HELPER = """        DEFINE('SNBassert(SNBav,SNBev,SNBn)')    :(SNBaEnd)
SNBassert
        IDENT(SNBav, SNBev)                        :S(RETURN)
        OUTPUT = 'FAIL ' SNBn
        :(RETURN)
SNBaEnd"""


def build_ssa_chain(subexprs):
    """
    Build SSA-style temp variable chain from ordered sub-expressions.
    Each sub-expression gets a temp T1..Tn.
    Each Tn's RHS replaces any prior sub-expression text with its temp name,
    so we use already-computed temps instead of re-evaluating.

    Returns list of (temp_name, rhs_expr, original_expr) tuples.
    """
    chain = []         # (temp, rhs, original)
    sub_to_temp = {}   # original_expr -> temp_name

    for i, sub in enumerate(subexprs):
        temp = f'SNBt{i+1}'
        # Build RHS: replace any previously seen sub-expression with its temp.
        # Replace longest matches first to avoid partial substitutions.
        rhs = sub
        for prev_sub, prev_temp in sorted(sub_to_temp.items(),
                                           key=lambda x: -len(x[0])):
            rhs = rhs.replace(prev_sub, prev_temp)
        chain.append((temp, rhs, sub))
        sub_to_temp[sub] = temp

    return chain


def emit_snippet(out_dir, test_name, scalars, subexprs, expr_vals,
                 source_file, line_no, raw_line, driver_src, stlimit):
    """
    Emit in-context regression test with SSA-style temp chain.

    Structure:
      [driver to stlimit=N]
      [assert helper SNBassert defined once]
      T1 = innermost_expr          :F(SNBf1)
      T2 = expr_using_T1           :F(SNBf2)
      ...
      Tn = full_expr_using_Tn-1    :F(SNBfn)
      SNBassert(T1, oracle1, 1)
      SNBassert(DATATYPE(T2), oracle2, 2)
      ...
      OUTPUT = 'PASS'              :(SNBend)
      SNBf1  OUTPUT = 'FAIL T1 FAILED'   :(SNBend)
      SNBf2  OUTPUT = 'FAIL T2 FAILED'   :(SNBend)
      ...
      SNBend
    END
    """
    safe_driver = '\n'.join(
        line for line in driver_src.splitlines()
        if not any(ci in line for ci in CRASH_INCLUDES)
    )

    # Build SSA chain from sub-expressions that have oracle values
    # Only include sub-expressions we actually captured oracle values for
    probed = [s for s in subexprs if s in expr_vals]
    if not probed: return False

    chain = build_ssa_chain(probed)

    lines = [
        f"* In-context snippet: {Path(source_file).name} line {line_no}",
        f"* Statement: {raw_line.strip()[:100]}",
        f"        &STLIMIT = {stlimit}",
        f"        &ERRLIMIT = 1",
        f"        &TRIM = 1",
        safe_driver,
        f"        &STLIMIT = 10000000",
        ASSERT_HELPER,
        f"* ── SSA temps (innermost first, each with fail branch) ──",
    ]

    # Temp assignments with fail gotos
    for temp, rhs, orig in chain:
        lines.append(f"        {temp} = ({rhs})   :F(SNBf_{temp})")

    lines.append(f"* ── asserts ──")

    # Assert block using temps
    for n, (temp, rhs, orig) in enumerate(chain, 1):
        entry = expr_vals.get(orig)
        if entry is None: continue
        kind, val = entry
        if kind == '__TYPE__':
            lines.append(f"        SNBassert(DATATYPE({temp}), '{val}', {n})")
        else:
            safe_val = val.replace("'", "''")
            lines.append(f"        SNBassert({temp}, '{safe_val}', {n})")

    lines.append(f"        OUTPUT = 'PASS'   :(SNBend)")

    # Fail labels at bottom
    lines.append(f"* ── fail labels ──")
    for temp, rhs, orig in chain:
        short = orig[:40].replace("'", "")
        lines.append(
            f"SNBf_{temp}   OUTPUT = 'FAIL {temp} FAILED [{short}]'   :(SNBend)"
        )

    lines += [f"SNBend", "END"]

    sno = '\n'.join(lines) + '\n'
    Path(out_dir, f'{test_name}.sno').write_text(sno)
    Path(out_dir, f'{test_name}.ref').write_text('PASS\n')
    return True


def enclosing_function(lines, target_line_no):
    """
    Find the function name and args for the function body containing target_line_no.
    Strategy: scan backwards for a line with a label that matches a DEFINE name.
    Returns (name, dummy_args_str) or (None, None).
    """
    define_re = re.compile(r"DEFINE\s*\(\s*'([A-Za-z][A-Za-z0-9]*)\(([^)]*)\)", re.I)
    label_re  = re.compile(r'^([A-Za-z][A-Za-z0-9]*)\s+')

    # Collect all DEFINE names and their arg lists in the file
    defines = {}
    for line in lines:
        m = define_re.search(line)
        if m:
            name = m.group(1)
            args = m.group(2)
            arg_list = [a.strip().split()[0] for a in args.split(',') if a.strip()]
            dummy_args = ', '.join("''" for _ in arg_list)
            defines[name.upper()] = (name, dummy_args)

    # Scan backwards from target line for a label matching a defined function
    for i in range(target_line_no - 2, -1, -1):
        m = label_re.match(lines[i])
        if m:
            label = m.group(1).upper()
            if label in defines:
                return defines[label]

    return None, None


def find_stlimit_for_line(driver_src, target_source_file, target_line_no, beauty_dir):
    """
    Find stlimit N just before the target line's first execution.
    Injects sentinel before target line, builds minimal probe with a function
    call to the enclosing function, runs under SPITBOL, captures &STCOUNT.
    """
    beauty_dir = Path(beauty_dir)
    lines = Path(target_source_file).read_text(errors='replace').splitlines()
    if target_line_no < 1 or target_line_no > len(lines):
        return 300

    sentinel = "        OUTPUT = 'XSENTINEL' &STCOUNT 'XSENTINELEND'"
    modified = lines[:]
    modified.insert(target_line_no - 1, sentinel)

    with tempfile.NamedTemporaryFile(mode='w', suffix='.sno', delete=False,
                                     dir='/tmp', prefix='bsg_inst_') as f:
        f.write('\n'.join(modified))
        inst_path = f.name

    prog = (
        f"-INCLUDE '{beauty_dir}/global.sno'\n"
        f"-INCLUDE '{inst_path}'\n"
        f"        &STLIMIT = 10000000\n"
        f"        &TRIM = 1\n"
        f"        $'#L' = 0\n"
        f"        $'$B' =\n"
        f"        $'$X' =\n"
        f"        $'$C' =\n"
        f"        $'@S' =\n"
        f"        $'#N' =\n"
        f"END\n"
    )
    out = run_sbl(prog, timeout=20)
    try: os.unlink(inst_path)
    except: pass

    if out:
        mm = re.search(r'XSENTINEL(\d+)XSENTINELEND', out)
        if mm:
            return max(1, int(mm.group(1)) - 1)

    return 300  # fallback


def generate_one(source_file, driver_path, line_no, beauty_dir, out_dir,
                 verbose=False):
    """Generate one snippet test for a given source file + line number."""
    beauty_dir = Path(beauty_dir); out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    lines = Path(source_file).read_text(errors='replace').splitlines()
    if line_no < 1 or line_no > len(lines):
        print(f"ERROR: line {line_no} out of range (file has {len(lines)} lines)")
        return False

    raw_line = lines[line_no - 1]
    if verbose: print(f"Target: {Path(source_file).name}:{line_no}: {raw_line.strip()}")

    subexprs = stmt_subexprs(raw_line)
    if not subexprs:
        print(f"No sub-expressions found on line {line_no}")
        return False
    if verbose: print(f"Sub-expressions ({len(subexprs)}): {subexprs}")

    driver_src = build_driver_src(driver_path, beauty_dir)
    stlimit = find_stlimit_for_line(driver_src, source_file, line_no, beauty_dir)
    if verbose: print(f"Using stlimit={stlimit}")

    scalars, expr_vals = run_oracle_gauntlet(driver_src, stlimit, subexprs, beauty_dir)
    if verbose:
        print(f"Scalars from DUMP: {len(scalars)}")
        for sub, entry in expr_vals.items():
            print(f"  [{sub}] = {repr(entry)}")

    tag = f"{Path(source_file).stem}_L{line_no:04d}"
    ok = emit_snippet(out_dir, tag, scalars, subexprs, expr_vals,
                      source_file, line_no, raw_line, driver_src, stlimit)
    if ok:
        print(f"Emitted: {tag}.sno")
    else:
        print(f"Nothing to emit (no oracle values captured)")
    return ok

def generate_random(beauty_dir, out_dir, n_samples, verbose=False, seed=42):
    """Sample random statements from subsystem files, generate tests."""
    rng = random.Random(seed)
    beauty_dir = Path(beauty_dir)

    # Subsystem files (not drivers, not global.sno)
    subsys_files = [f for f in beauty_dir.glob('*.sno')
                    if not f.name.startswith('beauty_')
                    and f.name != 'global.sno'
                    and f.name != 'FENCE.sno']

    generated = 0
    for _ in range(n_samples):
        src_file = rng.choice(subsys_files)
        lines = src_file.read_text(errors='replace').splitlines()
        # Pick a non-comment, non-define, non-blank line
        candidates = [
            (i+1, l) for i,l in enumerate(lines)
            if l.strip() and not l.strip().startswith('*')
            and not re.match(r'\s*DEFINE\s*\(', l, re.I)
            and not re.match(r'\s*DATA\s*\(', l, re.I)
            and '=' in l
        ]
        if not candidates: continue
        line_no, raw = rng.choice(candidates)

        # Find driver for this subsystem
        driver = beauty_dir / f'beauty_{src_file.stem}_driver.sno'
        if not driver.exists():
            # Try case-insensitive
            matches = list(beauty_dir.glob(f'beauty_*_driver.sno'))
            driver = next((d for d in matches
                           if src_file.stem.lower() in d.name.lower()), None)
        if not driver:
            if verbose: print(f"No driver for {src_file.name}, skip")
            continue

        if verbose: print(f"\n--- {src_file.name}:{line_no} ---")
        ok = generate_one(str(src_file), str(driver), line_no,
                          beauty_dir, out_dir, verbose)
        if ok: generated += 1

    print(f"\nGenerated {generated}/{n_samples} tests → {out_dir}")
    return generated

def run_suite(out_dir, verbose=False):
    """Run all snippet tests under scrip --ir-run."""
    tests = sorted(Path(out_dir).glob('*.sno'))
    passed = failed = timeouts = 0
    for sno in tests:
        ref = sno.with_suffix('.ref')
        if not ref.exists(): continue
        want = ref.read_text().strip()
        out = run_scrip(sno.read_text())
        got = (out or '').splitlines()
        last = got[-1].strip() if got else ''
        if last == want:
            passed += 1
            if verbose: print(f'PASS {sno.name}')
        elif out is None:
            timeouts += 1; print(f'TIMEOUT {sno.name}')
        else:
            failed += 1
            print(f'FAIL {sno.name}')
            if verbose and got:
                for l in got[-3:]: print(f'  {l}')
    print(f'--- PASS={passed} FAIL={failed} TIMEOUT={timeouts}')

def main():
    ap = argparse.ArgumentParser(description=__doc__,
             formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--beauty', default=str(BEAUTY))
    ap.add_argument('--out',    default=str(OUT))
    ap.add_argument('--source', help='Subsystem .sno file')
    ap.add_argument('--driver', help='Driver .sno file')
    ap.add_argument('--line',   type=int, help='Line number in source file')
    ap.add_argument('--samples',type=int, default=10)
    ap.add_argument('--run',    action='store_true')
    ap.add_argument('--verbose','-v', action='store_true')
    ap.add_argument('--seed',   type=int, default=42)
    args = ap.parse_args()

    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)

    if args.source and args.line:
        driver = args.driver or str(
            Path(args.beauty) / f'beauty_{Path(args.source).stem}_driver.sno')
        generate_one(args.source, driver, args.line,
                     args.beauty, args.out, args.verbose)
    else:
        generate_random(args.beauty, args.out, args.samples,
                        args.verbose, args.seed)

    if args.run:
        print('\n--- scrip --ir-run ---')
        run_suite(args.out, args.verbose)

if __name__ == '__main__':
    main()
