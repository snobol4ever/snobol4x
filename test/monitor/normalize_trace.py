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

RE_ASM_VAR  = re.compile(r'^VAR\s+(\S+)\s+"(.*)"$')
RE_ASM_STNO = re.compile(r'^STNO\s+\d+$')

def parse_asm_lines(lines):
    """Parse ASM comm_var stream. Returns list of (kind, name, value) events."""
    past_init = False
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
    if len(sys.argv) < 6:
        print('Usage: normalize_trace.py <conf> <csn_trace> <asm_trace> <csn_norm> <asm_norm>',
              file=sys.stderr)
        sys.exit(1)

    conf_path = sys.argv[1]
    csn_in    = sys.argv[2]
    asm_in    = sys.argv[3]
    csn_out   = sys.argv[4]
    asm_out   = sys.argv[5]

    include_rules, exclude_rules, ignore_rules = load_conf(conf_path)

    with open(csn_in) as f:
        csn_lines = f.readlines()
    with open(asm_in) as f:
        asm_lines = f.readlines()

    csn_events = normalize_csn(csn_lines, include_rules, exclude_rules, ignore_rules)
    asm_events = normalize_asm(asm_lines, include_rules, exclude_rules, ignore_rules)

    with open(csn_out, 'w') as f:
        f.write('\n'.join(csn_events) + ('\n' if csn_events else ''))
    with open(asm_out, 'w') as f:
        f.write('\n'.join(asm_events) + ('\n' if asm_events else ''))


if __name__ == '__main__':
    main()
