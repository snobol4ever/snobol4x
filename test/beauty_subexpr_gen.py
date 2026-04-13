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

def dump_val_to_snobol(name, raw_val):
    """
    Convert a DUMP value line to a SNOBOL4 assignment statement.
    raw_val is the text after '=' in the dump, e.g. '42' or "'hello'" or ''
    Returns None if we can't reconstruct (DATA struct reference).
    """
    # Skip DATA struct references like "link_counter #1"
    if re.search(r'#\d+', raw_val): return None
    # Integer
    if re.match(r'^-?\d+$', raw_val.strip()):
        return f"        {name} = {raw_val.strip()}"
    # String: already quoted in dump as 'val'
    if raw_val.startswith("'"):
        return f"        {name} = {raw_val}"
    # Empty (unset variable)
    if raw_val == '':
        return f"        {name} = ''"
    return None

# ── Run 1: Oracle gauntlet ────────────────────────────────────────────────────

MS = 'XGSSTART'; ME = 'XGSEND'  # gauntlet markers

def build_gauntlet_lines(subexprs, label_base):
    """
    Build SNOBOL4 OUTPUT lines that print each sub-expression's value,
    wrapped in markers so we can parse them out.
    For pattern-type expressions use DATATYPE(); for others use the value directly.
    """
    lines = []
    for i, sub in enumerate(subexprs):
        # We don't know type ahead of time -- just output the value.
        # If it's a pattern, OUTPUT will print something (SPITBOL shows pattern type).
        # Wrap in markers.
        lines.append(
            f"        OUTPUT = '{MS}{i}|' ({sub}) '{ME}'"
        )
    return lines

def run_oracle_gauntlet(driver_src, stlimit, subexprs, beauty_dir):
    """
    Run 1: driver to stlimit, then gauntlet.
    Returns (dump_scalars, expr_values) where expr_values = {sub: value_str}.
    """
    gauntlet = '\n'.join(build_gauntlet_lines(subexprs, 'G'))
    prog = (
        f"        &STLIMIT = {stlimit}\n"
        f"        &DUMP = 2\n"
        f"        &ERRLIMIT = 20\n"
        f"        &TRIM = 1\n"
        f"{driver_src}\n"
        f"        &STLIMIT = 10000000\n"
        f"{gauntlet}\n"
        f"END\n"
    )
    out = run_sbl(prog)
    if out is None: return {}, {}

    # Parse DUMP
    scalars = parse_dump(out)

    # Parse gauntlet output
    expr_vals = {}
    for i, sub in enumerate(subexprs):
        pat = re.compile(
            re.escape(f'{MS}{i}|') + r'(.*?)' + re.escape(ME), re.DOTALL)
        mm = pat.search(out)
        if mm: expr_vals[sub] = mm.group(1)

    return scalars, expr_vals

# ── Run 2: Isolated snippet emitter ───────────────────────────────────────────

def make_alias(name):
    """Make a safe SNOBOL4 variable alias for indirect names like $B, #L, @S."""
    return 'SNB' + re.sub(r'[^A-Za-z0-9]', 'x', name)

def emit_snippet(out_dir, test_name, scalars, subexprs, expr_vals,
                 source_file, line_no, raw_line):
    """
    Emit isolated snippet .sno + .ref.
    State block: assign scalars from DUMP.
    Assert block: IDENT per sub-expression innermost-first.
    """
    # ── State block ──
    state_lines = [
        f"* Isolated snippet: {Path(source_file).name} line {line_no}",
        f"* Statement: {raw_line.strip()[:100]}",
        f"        &TRIM = 1",
        f"        &ERRLIMIT = 20",
    ]

    # Track which indirect names need aliases
    aliases = {}  # indirect_name -> alias_var

    for name, raw_val in sorted(scalars.items()):
        stmt = dump_val_to_snobol(name, raw_val)
        if stmt:
            state_lines.append(stmt)
        # Check if name is an indirect var ($B, #L etc.)
        # These appear in DUMP as the bare name e.g. '$B' = 'hello'
        # In state block we need:  bB = '$B'  \n  $bB = 'hello'
        if re.match(r'^[$#@]', name):
            alias = make_alias(name)
            aliases[name] = alias
            quoted = "'" + name.replace("'","''") + "'"
            state_lines.append(f"        {alias} = {quoted}")
            if stmt:
                # Replace direct assignment with indirect
                val_part = raw_val if raw_val else "''"
                state_lines[-3] = f"        ${alias} = {val_part}"  # overwrite

    # ── Assert block ──
    assert_lines = ["* ── gauntlet asserts ──"]
    label_n = 1
    has_assert = False

    for i, sub in enumerate(subexprs):
        val = expr_vals.get(sub)
        if val is None: continue  # couldn't probe — skip

        # Replace indirect name references in sub with aliases
        sub_safe = sub
        for iname, alias in aliases.items():
            # Replace $'name' with $alias in sub_safe
            quoted = "'" + iname.replace("'","''") + "'"
            sub_safe = sub_safe.replace(f'${quoted}', f'${alias}')

        safe_val = val.replace("'", "''")
        lpass = f'SNT{label_n}p'; lfail = f'SNT{label_n}f'
        assert_lines.append(
            f"        IDENT(({sub_safe}), '{safe_val}')   :S({lpass})F({lfail})"
        )
        assert_lines.append(
            f"{lfail}   OUTPUT = 'FAIL [{sub[:50]}] got=[' ({sub_safe}) '] want=[{safe_val}]'"
        )
        assert_lines.append(f"        :(SNTend)")
        assert_lines.append(f"{lpass}")
        label_n += 1
        has_assert = True

    if not has_assert:
        return False  # nothing to emit

    assert_lines.append("        OUTPUT = 'PASS'")
    assert_lines.append("SNTend")

    sno = '\n'.join(state_lines + [''] + assert_lines + ['END\n'])
    Path(out_dir, f'{test_name}.sno').write_text(sno)
    Path(out_dir, f'{test_name}.ref').write_text('PASS\n')
    return True

# ── Find stlimit for a statement ──────────────────────────────────────────────

def find_stlimit_for_line(driver_src, target_source_file, target_line_no, beauty_dir):
    """
    Find the &STCOUNT value just before the target line executes.
    Strategy: instrument with TRACE on that line's label, or binary search.
    For now: run driver fully, collect total stmts, sample at 60% as heuristic.
    TODO: proper instrumentation.
    """
    prog = (
        f"        &STLIMIT = 10000000\n"
        f"        &TRIM = 1\n"
        f"{driver_src}\n"
        f"        OUTPUT = 'XSTC' &STCOUNT 'XSTCEND'\n"
        f"END\n"
    )
    out = run_sbl(prog)
    if out is None: return 300
    mm = re.search(r'XSTC(\d+)XSTCEND', out)
    total = int(mm.group(1)) if mm else 500
    # Skip first 15% (global.sno init), use 40-80% range for interesting samples
    return int(total * 0.5)

# ── Main generator ────────────────────────────────────────────────────────────

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
        for sub, val in expr_vals.items():
            print(f"  [{sub}] = {repr(val)}")

    tag = f"{Path(source_file).stem}_L{line_no:04d}"
    ok = emit_snippet(out_dir, tag, scalars, subexprs, expr_vals,
                      source_file, line_no, raw_line)
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
