#!/usr/bin/env python3
"""
inject_traces.py — instrument a .sno file with TRACE() calls.

Usage: python3 inject_traces.py <sno_file> [tracepoints_conf]

Strategy:
  - Injects three SNOBOL4 callback functions (MONCALL, MONRET, MONVAL) that
    write trace events to TERMINAL (stderr), keeping program stdout clean.
  - Sets &TRACE = 999999999 so TRACE events fire.
  - Injects TRACE() registration calls for all included functions/variables.
  - Output is a valid .sno file on stdout.

Trace output format on stderr:
    CALL   funcname
    RETURN funcname = retval
    VALUE  varname = value
"""

import sys
import re
import os

# ---------------------------------------------------------------------------
# Parse tracepoints.conf
# ---------------------------------------------------------------------------

def load_conf(path):
    include_rules = []
    exclude_rules = []
    ignore_rules  = []

    if not os.path.exists(path):
        include_rules.append(re.compile(r'.*'))
        return include_rules, exclude_rules, ignore_rules

    with open(path) as f:
        for raw in f:
            line = raw.split('#')[0].strip()
            if not line:
                continue
            parts = line.split()
            if not parts:
                continue
            verb = parts[0].upper()
            if verb == 'INCLUDE' and len(parts) >= 2:
                include_rules.append(re.compile(parts[1], re.IGNORECASE))
            elif verb == 'EXCLUDE' and len(parts) >= 2:
                exclude_rules.append(re.compile(parts[1], re.IGNORECASE))
            elif verb == 'IGNORE' and len(parts) >= 3:
                ignore_rules.append((
                    re.compile(parts[1], re.IGNORECASE),
                    re.compile(parts[2], re.IGNORECASE),
                ))

    return include_rules, exclude_rules, ignore_rules


def is_included(name, include_rules, exclude_rules):
    for rx in exclude_rules:
        if rx.fullmatch(name) or rx.search(name):
            return False
    for rx in include_rules:
        if rx.fullmatch(name) or rx.search(name):
            return True
    return False


# ---------------------------------------------------------------------------
# Scan .sno source
# ---------------------------------------------------------------------------

RE_DEFINE = re.compile(
    r"DEFINE\s*\(\s*'([A-Za-z][A-Za-z0-9]*)\s*\(",
    re.IGNORECASE
)

RE_ASSIGN_LHS = re.compile(
    r'^\s{1,}([A-Za-z][A-Za-z0-9]*|&[A-Za-z][A-Za-z0-9]*)\s*='
)

RE_LABEL_LINE = re.compile(r'^[A-Za-z][A-Za-z0-9]*[\s]')

INTERNAL_FNS = {'MONCALL', 'MONRET', 'MONVAL'}


def scan_sno(lines, include_rules, exclude_rules):
    functions = []
    variables = []
    seen_fn   = set()
    seen_var  = set()

    for line in lines:
        stripped = line.strip()
        if stripped.startswith('*') or stripped.startswith('-'):
            continue

        for m in RE_DEFINE.finditer(line):
            fn = m.group(1)
            if fn.upper() in INTERNAL_FNS:
                continue
            if fn not in seen_fn and is_included(fn, include_rules, exclude_rules):
                seen_fn.add(fn)
                functions.append(fn)

        if not RE_LABEL_LINE.match(line):
            m = RE_ASSIGN_LHS.match(line)
            if m:
                var = m.group(1)
                if var not in seen_var and is_included(var, include_rules, exclude_rules):
                    seen_var.add(var)
                    variables.append(var)

    return functions, variables


# ---------------------------------------------------------------------------
# Monitor preamble and registrations
# ---------------------------------------------------------------------------

MONITOR_PREAMBLE = """\
* --- MONITOR PREAMBLE: injected by inject_traces.py ---
        &TRACE         =  999999999
        DEFINE('MONCALL(MONN,MONT)')                :(MONCALL_END)
MONCALL TERMINAL       =  'CALL ' MONN              :(RETURN)
MONCALL_END
        DEFINE('MONRET(MONN,MONT)MONV')             :(MONRET_END)
MONRET  MONV           =  CONVERT(VALUE(MONN),'STRING')  :F(MONRET_NR)
        TERMINAL       =  'RETURN ' MONN ' = ' MONV :(RETURN)
MONRET_NR
        TERMINAL       =  'RETURN ' MONN            :(RETURN)
MONRET_END
        DEFINE('MONVAL(MONN,MONT)MONV')             :(MONVAL_END)
MONVAL  MONV           =  CONVERT(VALUE(MONN),'STRING')  :F(MONVAL_U)
        TERMINAL       =  'VALUE ' MONN ' = ' MONV  :(RETURN)
MONVAL_U
        TERMINAL       =  'VALUE ' MONN ' = (undef)' :(RETURN)
MONVAL_END
* --- MONITOR PREAMBLE END ---
"""


def build_trace_registrations(functions, variables):
    lines = ['* --- MONITOR: TRACE registrations ---\n']
    for fn in functions:
        lines.append(f"        TRACE('{fn}','CALL',   '','MONCALL')\n")
        lines.append(f"        TRACE('{fn}','RETURN', '','MONRET')\n")
    for var in variables:
        lines.append(f"        TRACE('{var}','VALUE',  '','MONVAL')\n")
    lines.append('* --- MONITOR: end TRACE registrations ---\n')
    return lines


# ---------------------------------------------------------------------------
# Emit instrumented .sno
# ---------------------------------------------------------------------------

def emit_instrumented(lines, functions, variables, ignore_rules, out):
    # Emit ignore-rule table as SNOBOL4 comments (read by normalize_trace.py)
    out.write('* MONITOR-IGNORE-BEGIN\n')
    for name_rx, val_rx in ignore_rules:
        out.write(f'* IGNORE  {name_rx.pattern}  {val_rx.pattern}\n')
    out.write('* MONITOR-IGNORE-END\n')

    # Split: everything up to and including last -directive goes first
    split = 0
    for i, line in enumerate(lines):
        if line.strip().startswith('-'):
            split = i + 1

    for line in lines[:split]:
        out.write(line)

    out.write(MONITOR_PREAMBLE)

    if functions or variables:
        for reg_line in build_trace_registrations(functions, variables):
            out.write(reg_line)

    for line in lines[split:]:
        out.write(line)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print('Usage: inject_traces.py <sno_file> [tracepoints_conf]', file=sys.stderr)
        sys.exit(1)

    sno_path  = sys.argv[1]
    conf_path = sys.argv[2] if len(sys.argv) > 2 else \
                os.path.join(os.path.dirname(__file__), 'tracepoints.conf')

    include_rules, exclude_rules, ignore_rules = load_conf(conf_path)

    with open(sno_path) as f:
        lines = f.readlines()

    functions, variables = scan_sno(lines, include_rules, exclude_rules)
    emit_instrumented(lines, functions, variables, ignore_rules, sys.stdout)


if __name__ == '__main__':
    main()
