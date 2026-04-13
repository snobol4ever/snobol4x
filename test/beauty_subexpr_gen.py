#!/usr/bin/env python3
"""
beauty_subexpr_gen.py — Sub-expression oracle test generator.

APPROACH: random stlimit sampling + arbitrary statement probing.

1. Pick a random stlimit N within the driver's execution range.
2. Run driver to N — establishes live context (globals, DATA, functions).
3. Pick a statement from a beauty subsystem file.
4. Probe that statement's sub-expressions (via SNBprobe helper).
5. If results are non-trivial → build SSA chain + emit test.
6. If boring/all-fail → try different N or statement.

This decouples context from statement: any stlimit past the includes
gives a valid context for probing any subsystem statement.
No sentinel injection, no enclosing-function detection needed.
"""

import re, sys, os, subprocess, tempfile, argparse, random
from pathlib import Path

SBL   = '/home/claude/x64/bin/sbl'
SCRIP = '/home/claude/one4all/scrip'
BEAUTY = Path('/home/claude/corpus/programs/snobol4/beauty')
OUT    = Path('/home/claude/corpus/programs/snobol4/subexpr')

CRASH_INCLUDES = {'FENCE.sno', 'io.sno'}

# ─── Tokenizer ────────────────────────────────────────────────────────────────

TOKEN_RE = re.compile(
    r"('(?:[^']|'')*')"             # g1  STR
    r'|(\d+(?:\.\d+)?)'            # g2  NUM
    r'|([A-Za-z][A-Za-z0-9]*)'    # g3  ID
    r'|(\*\*)'                     # g4  STARSTAR
    r'|(\*)'                       # g5  STAR
    r'|([+\-/])'                   # g6  ARITH
    r'|(&[A-Za-z][A-Za-z0-9]*)'  # g7  KW
    r'|(\$)'                       # g8  DREF
    r"|(\.'(?:[^']|'')*')"         # g9  NAMELIT
    r'|(\.[A-Za-z][A-Za-z0-9]*)' # g10 NAMEREF
    r'|(@[A-Za-z][A-Za-z0-9]*)' # g11 CURSOR
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

# ─── Sub-expression extractor ─────────────────────────────────────────────────

class SubP:
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
        parts = []
        while not self.stop():
            k,v = self.peek()
            if v == '.' and k == 'OTHER':
                self.eat(); tgt = self._primary()
                base = ' '.join(parts) if parts else ''
                expr = f'{base} . {tgt}' if base else f'. {tgt}'
                self.subs.append(expr); parts = [expr]; continue
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
    toks = tokenize(text)
    p = SubP(toks); p.parse_top()
    seen = set(); result = []
    for s in p.subs:
        s = s.strip()
        if not s or s in seen: continue
        seen.add(s)
        if re.match(r"^'[^']*'$", s): continue
        if re.match(r'^\d+$', s):     continue
        if re.match(r'^&\w+$', s):    continue
        if re.match(r'^\.[A-Za-z]',s):continue
        if re.match(r'^@[A-Za-z]',s): continue
        if re.match(r'^[A-Za-z]\w*$',s): continue
        result.append(s)
    return result

def stmt_subexprs(raw_line):
    """Extract sub-expressions from subject, pattern, and replacement fields."""
    line = raw_line.strip()
    m = re.match(r'^[A-Za-z][A-Za-z0-9]*\s+(.*)', line)
    body = m.group(1) if m else line
    goto = re.search(r'\s*:[SF(]', body)
    if goto: body = body[:goto.start()]
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

def is_interesting(raw_line):
    """True if this line is worth probing (has operators, not a define/comment/goto)."""
    ls = raw_line.strip()
    if not ls or ls.startswith('*'): return False
    if re.match(r'^-INCLUDE|^\s*END\s*$', ls, re.I): return False
    if re.match(r'\s*DEFINE\s*\(', ls, re.I): return False
    if re.match(r'\s*DATA\s*\(', ls, re.I): return False
    # Must have at least one interesting operator
    return any(ch in ls for ch in '()$+-*/.|')

# ─── Safe driver builder ──────────────────────────────────────────────────────

def build_safe_driver(driver_path, beauty_dir):
    """Expand includes, strip crash includes and &STLIMIT lines."""
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

# ─── SPITBOL runner ───────────────────────────────────────────────────────────

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
        r = subprocess.run([SCRIP, '--ir-run', fname], capture_output=True,
                           timeout=timeout, cwd='/home/claude/one4all')
        return r.stdout.decode('utf-8', errors='replace') if r.stdout else ''
    except subprocess.TimeoutExpired:
        return None
    finally:
        try: os.unlink(fname)
        except: pass

# ─── Oracle gauntlet ──────────────────────────────────────────────────────────

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

PROBE_RE = re.compile(r'XGSSTART\|([^|]+)\|(.+?)\|XGSEND', re.MULTILINE)

def run_gauntlet(safe_driver, stlimit, subexprs):
    """
    Run driver to stlimit, then probe each sub-expression.
    Returns {sub: (kind, val)} dict. kind = '__TYPE__' or '__VALUE__'.
    """
    probes = '\n'.join(f"        SNBprobe({sub})" for sub in subexprs)
    prog = (
        f"        &STLIMIT = {stlimit}\n"
        f"        &ERRLIMIT = 1\n"
        f"        &TRIM = 1\n"
        f"{safe_driver}\n"
        f"        &STLIMIT = 10000000\n"
        f"{PROBE_HELPER}\n"
        f"{probes}\n"
        f"END\n"
    )
    out = run_sbl(prog)
    if not out: return {}
    matches = PROBE_RE.findall(out)
    result = {}
    for i, sub in enumerate(subexprs):
        if i >= len(matches): break
        typ, rest = matches[i]
        if typ in ('pattern', 'expression', 'code'):
            result[sub] = ('__TYPE__', typ)
        else:
            pipe = rest.find('|')
            val = rest[pipe+1:] if pipe >= 0 else rest
            result[sub] = ('__VALUE__', val)
    return result

# ─── SSA chain builder ────────────────────────────────────────────────────────

def build_ssa_chain(subexprs):
    """
    SSA-style temp vars. Each Tn's RHS replaces prior sub-exprs with temps.
    Returns list of (temp, rhs, original_expr).
    """
    chain = []; sub_to_temp = {}
    for i, sub in enumerate(subexprs):
        temp = f'SNBt{i+1}'
        rhs = sub
        for prev, pt in sorted(sub_to_temp.items(), key=lambda x: -len(x[0])):
            rhs = rhs.replace(prev, pt)
        chain.append((temp, rhs, sub))
        sub_to_temp[sub] = temp
    return chain

# ─── Assert helper ────────────────────────────────────────────────────────────

ASSERT_HELPER = """        DEFINE('SNBassert(SNBav,SNBev,SNBn)')    :(SNBaEnd)
SNBassert
        IDENT(SNBav, SNBev)                        :S(RETURN)
        OUTPUT = 'FAIL ' SNBn
        :(RETURN)
SNBaEnd"""

# ─── Test emitter ─────────────────────────────────────────────────────────────

def emit_test(out_dir, test_name, safe_driver, stlimit, subexprs, expr_vals,
              source_file, line_no, raw_line):
    """
    Emit in-context test: driver to stlimit=N, then interleaved SSA+assert.
    """
    probed = [s for s in subexprs if s in expr_vals]
    if not probed: return False
    chain = build_ssa_chain(probed)

    lines = [
        f"* {Path(source_file).name} L{line_no}: {raw_line.strip()[:80]}",
        f"        &STLIMIT = {stlimit}",
        f"        &ERRLIMIT = 1",
        f"        &TRIM = 1",
        safe_driver,
        f"        &STLIMIT = 10000000",
        ASSERT_HELPER,
    ]

    for n, (temp, rhs, orig) in enumerate(chain, 1):
        entry = expr_vals.get(orig)
        lines.append(f"        {temp} = ({rhs})   :F(SNBf{n})")
        if entry:
            kind, val = entry
            if kind == '__TYPE__':
                lines.append(f"        SNBassert(DATATYPE({temp}), '{val}', {n})")
            else:
                lines.append(f"        SNBassert({temp}, '{val.replace(chr(39),chr(39)*2)}', {n})")

    lines.append("        OUTPUT = 'PASS'   :(SNBend)")
    for n in range(1, len(chain)+1):
        lines.append(f"SNBf{n}  OUTPUT = 'FAIL {n}'   :(SNBend)")
    lines += ["SNBend", "END"]

    sno = '\n'.join(lines) + '\n'
    Path(out_dir, f'{test_name}.sno').write_text(sno)
    Path(out_dir, f'{test_name}.ref').write_text('PASS\n')
    return True

# ─── Driver total stcount ─────────────────────────────────────────────────────

def driver_total_stmts(safe_driver):
    """Run driver fully, return (lower_bound, total_stmts).
    lower_bound = stcount after all includes complete (bomb sentinel).
    total_stmts = stcount at end of full driver run.
    """
    # Bomb sentinel: inject OUTPUT right after the last -INCLUDE line
    lines = safe_driver.splitlines()
    last_inc = max((i for i,l in enumerate(lines)
                    if re.match(r'\s*-INCLUDE', l.strip(), re.I)), default=-1)
    bomb_lines = lines[:]
    bomb_lines.insert(last_inc + 1,
        "        OUTPUT = 'XLOWER' &STCOUNT 'XLOWER'")
    bomb_driver = '\n'.join(bomb_lines)

    prog = (
        f"        &STLIMIT = 10000000\n"
        f"        &TRIM = 1\n"
        f"{bomb_driver}\n"
        f"        OUTPUT = 'XSTC' &STCOUNT 'XEND'\n"
        f"END\n"
    )
    out = run_sbl(prog, timeout=30)
    mm_lo = re.search(r'XLOWER(\d+)XLOWER', out or '')
    mm_tot = re.search(r'XSTC(\d+)XEND', out or '')
    lo  = int(mm_lo.group(1))  if mm_lo  else 100
    tot = int(mm_tot.group(1)) if mm_tot else 500
    return lo, tot

# ─── Nth-iteration sentinel ───────────────────────────────────────────────────

def find_nth_stlimit(safe_driver, source_file, line_no, n=1,
                     outer_n=None, verbose=False):
    """
    Find the &STCOUNT at the Nth crossing of source_file:line_no.

    Injects a counter-gated bomb sentinel before target line (by line number,
    not string matching). Runs the instrumented program. Captures &STCOUNT
    when the counter reaches N. Returns stlimit = &STCOUNT - 1.

    For nested loops: outer_n specifies the outer counter target.
    Both counters must reach their targets simultaneously to fire.

    The bomb sentinel injected before target line:
        SNBcnt = SNBcnt + 1
        EQ(SNBcnt, N)              :F(SNBskip_N)
        OUTPUT = 'XBOMB' &STCOUNT 'XBOMBEND'
        SNBskip_N
    """
    lines = Path(source_file).read_text(errors='replace').splitlines()
    if line_no < 1 or line_no > len(lines):
        return None

    # Build counter-gated bomb — unique labels using line_no + n
    tag = f"{line_no}_{n}"
    nl = "\n"
    if outer_n is not None:
        bomb = (
            f"        SNBout{tag} = SNBout{tag} + 1" + nl +
            f"        SNBin{tag}  = SNBin{tag}  + 1" + nl +
            f"        EQ(SNBout{tag}, {outer_n}) EQ(SNBin{tag}, {n})   :F(SNBskip{tag})" + nl +
            f"        OUTPUT = 'XBOMB' &STCOUNT 'XBOMBEND'" + nl +
            f"SNBskip{tag}"
        )
    else:
        bomb = (
            f"        SNBcnt{tag} = SNBcnt{tag} + 1" + nl +
            f"        EQ(SNBcnt{tag}, {n})       :F(SNBskip{tag})" + nl +
            f"        OUTPUT = 'XBOMB' &STCOUNT 'XBOMBEND'" + nl +
            f"SNBskip{tag}"
        )

    # Inject bomb before target line using line number indexing (not find/replace)
    modified = lines[:]
    modified.insert(line_no - 1, bomb)

    with tempfile.NamedTemporaryFile(mode='w', suffix='.sno', delete=False,
                                     dir='/tmp', prefix='bsg_nth_') as f:
        f.write('\n'.join(modified))
        inst_path = f.name

    # Replace source file reference in safe_driver with instrumented version
    inst_driver = safe_driver.replace(str(source_file), inst_path)

    prog = (
        f"        &STLIMIT = 10000000\n"
        f"        &TRIM = 1\n"
        f"{inst_driver}\n"
        f"END\n"
    )
    out = run_sbl(prog, timeout=30)
    try: os.unlink(inst_path)
    except: pass

    if not out:
        return None
    mm = re.search(r'XBOMB(\d+)XBOMBEND', out)
    if mm:
        stcount = int(mm.group(1))
        if verbose:
            print(f"  Bomb fired at &STCOUNT={stcount} (iteration {n})")
        return max(1, stcount - 1)
    return None



# ─── Main: random sampling ────────────────────────────────────────────────────

def generate_random(beauty_dir, out_dir, driver_path, subsys_files,
                    n_tests=20, attempts=100, verbose=False, seed=42):
    """
    Random stlimit sampling approach:
    1. Pick random stlimit N in driver's execution range
    2. Pick random statement from subsystem files
    3. Probe sub-expressions in that context
    4. If results interesting → emit test
    5. Repeat until n_tests generated or attempts exhausted
    """
    rng = random.Random(seed)
    beauty_dir = Path(beauty_dir); out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    safe_driver = build_safe_driver(driver_path, beauty_dir)
    lo, total = driver_total_stmts(safe_driver)
    # lo = after includes (bomb sentinel) — guaranteed all functions defined
    # hi = 95% of total — before driver epilogue
    hi = int(total * 0.95)
    if verbose: print(f"Driver stmts: {total}, include lower bound: {lo}, sampling [{lo}, {hi}]")

    # Load all candidate statements from subsystem files
    candidates = []  # (path, lineno, raw_line)
    for sf in subsys_files:
        path = Path(sf)
        if not path.exists(): continue
        for i, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
            if is_interesting(line):
                candidates.append((path, i, line))
    if verbose: print(f"Candidate statements: {len(candidates)}")

    generated = 0
    for attempt in range(attempts):
        if generated >= n_tests: break

        stlimit = rng.randint(lo, hi)
        path, line_no, raw_line = rng.choice(candidates)

        subexprs = stmt_subexprs(raw_line)
        if not subexprs:
            continue

        if verbose:
            print(f"  [{attempt+1}] stlimit={stlimit} {path.name}:{line_no}: {raw_line.strip()[:50]}")

        expr_vals = run_gauntlet(safe_driver, stlimit, subexprs)
        if not expr_vals:
            if verbose: print(f"    → no values")
            continue

        test_name = f"{path.stem}_L{line_no:04d}_sl{stlimit}"
        ok = emit_test(out_dir, test_name, safe_driver, stlimit,
                       subexprs, expr_vals, str(path), line_no, raw_line)
        if ok:
            generated += 1
            if verbose: print(f"    → EMIT ({len(expr_vals)} values)")
        else:
            if verbose: print(f"    → nothing to emit")

    print(f"Generated {generated}/{n_tests} tests in {out_dir}")
    return generated

# ─── Single statement (explicit) ─────────────────────────────────────────────

def generate_one(source_file, driver_path, line_no, beauty_dir, out_dir,
                 stlimit=None, verbose=False):
    beauty_dir = Path(beauty_dir); out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = Path(source_file).read_text(errors='replace').splitlines()
    if line_no < 1 or line_no > len(lines):
        print(f"Line {line_no} out of range"); return False
    raw_line = lines[line_no - 1]
    if verbose: print(f"Target: {Path(source_file).name}:{line_no}: {raw_line.strip()}")
    subexprs = stmt_subexprs(raw_line)
    if not subexprs: print("No sub-expressions"); return False
    if verbose: print(f"Sub-expressions: {subexprs}")

    safe_driver = build_safe_driver(driver_path, beauty_dir)
    if stlimit is None:
        lo, total = driver_total_stmts(safe_driver)
        stlimit = lo + (total - lo) // 2  # midpoint of useful range
    if verbose: print(f"stlimit={stlimit}")

    expr_vals = run_gauntlet(safe_driver, stlimit, subexprs)
    if verbose:
        for sub, entry in expr_vals.items():
            print(f"  {sub!r} → {entry}")

    tag = f"{Path(source_file).stem}_L{line_no:04d}_sl{stlimit}"
    ok = emit_test(out_dir, tag, safe_driver, stlimit,
                   subexprs, expr_vals, source_file, line_no, raw_line)
    print(f"{'Emitted' if ok else 'Nothing to emit'}: {tag}.sno")
    return ok

# ─── Suite runner ─────────────────────────────────────────────────────────────

def run_suite(out_dir, verbose=False):
    tests = sorted(Path(out_dir).glob('*.sno'))
    passed = failed = timeouts = 0
    for sno in tests:
        ref = sno.with_suffix('.ref')
        if not ref.exists(): continue
        want = ref.read_text().strip()
        out = run_scrip(sno.read_text())
        last = (out or '').splitlines()[-1].strip() if (out or '').strip() else ''
        if last == want:
            passed += 1
            if verbose: print(f'PASS {sno.name}')
        elif out is None:
            timeouts += 1; print(f'TIMEOUT {sno.name}')
        else:
            failed += 1; print(f'FAIL {sno.name}')
            if verbose: print(f'  last=[{last}] want=[{want}]')
    print(f'--- PASS={passed} FAIL={failed} TIMEOUT={timeouts}')

# ─── Main ─────────────────────────────────────────────────────────────────────

# Default subsystem files (all non-driver, non-global beauty .sno files)
DEFAULT_SUBSYS = [
    'Gen.sno','omega.sno','stack.sno','assign.sno','counter.sno',
    'tree.sno','Qize.sno','ShiftReduce.sno','TDump.sno','XDump.sno',
    'ReadWrite.sno','trace.sno','match.sno','case.sno','semantic.sno',
]

def main():
    ap = argparse.ArgumentParser(description=__doc__,
             formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--beauty', default=str(BEAUTY))
    ap.add_argument('--out',    default=str(OUT))
    ap.add_argument('--driver', help='Driver .sno file (for context)')
    ap.add_argument('--source', help='Subsystem .sno file (for --line mode)')
    ap.add_argument('--line',   type=int, help='Specific line number')
    ap.add_argument('--stlimit',type=int, default=None)
    ap.add_argument('--tests',  type=int, default=20, help='Tests to generate')
    ap.add_argument('--attempts',type=int, default=100)
    ap.add_argument('--subsys', nargs='*', help='Subsystem files to sample from')
    ap.add_argument('--run',    action='store_true')
    ap.add_argument('--verbose','-v', action='store_true')
    ap.add_argument('--seed',   type=int, default=42)
    args = ap.parse_args()

    beauty = Path(args.beauty); out = Path(args.out)

    if args.source and args.line:
        driver = args.driver or str(beauty / f'beauty_{Path(args.source).stem}_driver.sno')
        generate_one(args.source, driver, args.line, beauty, out,
                     args.stlimit, args.verbose)
    else:
        driver = args.driver or str(beauty / 'beauty_Gen_driver.sno')
        subsys = [str(beauty / f) for f in (args.subsys or DEFAULT_SUBSYS)]
        generate_random(beauty, out, driver, subsys,
                        args.tests, args.attempts, args.verbose, args.seed)

    if args.run:
        print('\n--- scrip --ir-run ---')
        run_suite(out, args.verbose)

if __name__ == '__main__':
    main()
