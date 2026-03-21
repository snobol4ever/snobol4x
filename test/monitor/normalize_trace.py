#!/usr/bin/env python3
"""
normalize_trace.py — normalize TRACE streams for diffing.

Usage: python3 normalize_trace.py <conf> <csn_trace> <asm_trace> <csn_norm> <asm_norm>

Two input formats:

CSNOBOL4 (via injected MONCALL/MONRET/MONVAL callbacks writing to TERMINAL/stderr):
    VALUE varname = value
    CALL  funcname
    RETURN funcname = retval

ASM backend (via MONITOR=1 env var → comm_var → stderr):
    STNO N          ← statement boundary (used to skip init noise)
    VAR name "value"

Both are normalized to:
    VALUE  name  value     (variable assignment)
    CALL   name            (function entry — ASM: not yet emitted)
    RETURN name = val      (function exit  — ASM: not yet emitted)

Variables are filtered against the INCLUDE/EXCLUDE rules from tracepoints.conf
so both streams only contain the same set of variables.
"""

import sys
import re
import os

# ---------------------------------------------------------------------------
# Parse tracepoints.conf for INCLUDE/EXCLUDE/IGNORE
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


def should_ignore(name, value, ignore_rules):
    for name_rx, val_rx in ignore_rules:
        if name_rx.search(name):
            if val_rx.pattern in ('.*', '*') or val_rx.search(value):
                return True
    return False


# ---------------------------------------------------------------------------
# CSNOBOL4 format: output from MONCALL/MONRET/MONVAL callbacks via TERMINAL
#   VALUE varname = value
#   CALL  funcname
#   RETURN funcname = retval
#   RETURN funcname
# ---------------------------------------------------------------------------

RE_CSN_VALUE  = re.compile(r'^VALUE\s+(\S+)\s*=\s*(.*?)\s*$')
RE_CSN_CALL   = re.compile(r'^CALL\s+(\S+)\s*$')
RE_CSN_RETURN = re.compile(r'^RETURN\s+(\S+?)(?:\s*=\s*(.*?))?\s*$')

def parse_csn_line(line):
    s = line.strip()
    m = RE_CSN_VALUE.match(s)
    if m:
        return ('VALUE', m.group(1), m.group(2))
    m = RE_CSN_CALL.match(s)
    if m:
        return ('CALL', m.group(1), '')
    m = RE_CSN_RETURN.match(s)
    if m:
        return ('RETURN', m.group(1), m.group(2) or '')
    return None


# ---------------------------------------------------------------------------
# ASM format: MONITOR=1 via comm_var
#   STNO N
#   VAR name "value"
# Skip all VAR lines before first STNO (init noise).
# ---------------------------------------------------------------------------

RE_ASM_VAR  = re.compile(r'^VAR\s+(\S+)\s+(?:"|\\u0022)(.*?)(?:"|\\u0022)$')
RE_ASM_STNO = re.compile(r'^STNO\s+\d+$')

def parse_asm_lines(lines):
    """Parse ASM/JVM/NET comm_var stream. Returns list of (kind, name, value) events.

    STNO gating: ASM emits runtime init VAR lines before STNO 1 (INC library
    constants).  Skip those.  JVM/NET emit no STNO — their stream starts clean,
    so if no STNO is present at all, accept everything from the start.
    """
    lines = list(lines)
    has_stno = any(RE_ASM_STNO.match(l.strip()) for l in lines)
    past_init = not has_stno   # if no STNO, accept from line 1
    events = []
    for line in lines:
        s = line.strip()
        if RE_ASM_STNO.match(s):
            past_init = True
            continue
        if not past_init:
            continue
        m = RE_ASM_VAR.match(s)
        if m:
            events.append(('VALUE', m.group(1), m.group(2)))
    return events


# ---------------------------------------------------------------------------
# Normalize a stream to canonical event strings
# ---------------------------------------------------------------------------

def normalize_csn(lines, include_rules, exclude_rules, ignore_rules):
    events = []
    for line in lines:
        ev = parse_csn_line(line.rstrip('\n'))
        if ev is None:
            continue
        kind, name, value = ev
        name = name.upper()   # fold: SPITBOL lowercase vs CSNOBOL4 uppercase
        if not is_included(name, include_rules, exclude_rules):
            continue
        if should_ignore(name, value, ignore_rules):
            continue
        if kind == 'VALUE':
            events.append(f'VALUE {name} = {value}')
        elif kind == 'CALL':
            events.append(f'CALL {name}')
        elif kind == 'RETURN':
            events.append(f'RETURN {name} = {value}' if value else f'RETURN {name}')
    return events


def normalize_asm(lines, include_rules, exclude_rules, ignore_rules):
    raw_events = parse_asm_lines(lines)
    events = []
    for kind, name, value in raw_events:
        name = name.upper()   # fold for consistency with CSN
        if not is_included(name, include_rules, exclude_rules):
            continue
        if should_ignore(name, value, ignore_rules):
            continue
        events.append(f'VALUE {name} = {value}')
    return events


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Two calling conventions:
    #   2-way (legacy): conf csn_in asm_in csn_out asm_out
    #   5-way:          conf csn_in spl_in asm_in jvm_in net_in
    #                        csn_out spl_out asm_out jvm_out net_out
    if len(sys.argv) not in (6, 12):
        print('Usage (2-way): normalize_trace.py <conf> <csn> <asm> <csn_out> <asm_out>',
              file=sys.stderr)
        print('Usage (5-way): normalize_trace.py <conf> <csn> <spl> <asm> <jvm> <net> '
              '<csn_out> <spl_out> <asm_out> <jvm_out> <net_out>', file=sys.stderr)
        sys.exit(1)

    conf_path = sys.argv[1]
    include_rules, exclude_rules, ignore_rules = load_conf(conf_path)

    if len(sys.argv) == 6:
        # 2-way legacy
        pairs = [
            (sys.argv[2], sys.argv[4], normalize_csn),
            (sys.argv[3], sys.argv[5], normalize_asm),
        ]
    else:
        # 5-way: inputs at [2..6], outputs at [7..11]
        # csn and spl use CSN format; asm/jvm/net use ASM/comm_var format
        pairs = [
            (sys.argv[2],  sys.argv[7],  normalize_csn),   # csn
            (sys.argv[3],  sys.argv[8],  normalize_csn),   # spl (same MONCALL format)
            (sys.argv[4],  sys.argv[9],  normalize_asm),   # asm
            (sys.argv[5],  sys.argv[10], normalize_asm),   # jvm
            (sys.argv[6],  sys.argv[11], normalize_asm),   # net
        ]

    for in_path, out_path, norm_fn in pairs:
        with open(in_path) as f:
            lines = f.readlines()
        events = norm_fn(lines, include_rules, exclude_rules, ignore_rules)
        with open(out_path, 'w') as f:
            f.write('\n'.join(events) + ('\n' if events else ''))


if __name__ == '__main__':
    main()
