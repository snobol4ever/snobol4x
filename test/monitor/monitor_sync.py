#!/usr/bin/env python3
"""
monitor_sync.py — synchronous barrier-step monitor controller.

Each participant blocks after each trace event waiting for a 1-byte ack.
Controller reads one event from each of N ready pipes, compares to oracle
(participant 0 = CSNOBOL4), sends 'G' (go) or 'S' (stop) to each go pipe.

Usage:
    monitor_sync.py <timeout> <names> <ready_pipes> <go_pipes>

    names       = comma-separated: csn,spl,asm,jvm,net
    ready_pipes = comma-separated paths (same order as names)
    go_pipes   = comma-separated paths (same order as names)

Exit 0 = all participants reached END agreeing with oracle.
Exit 1 = divergence detected (first diverging event printed).
Exit 2 = timeout or error.
"""

import sys
import os
import select
import time

import fcntl as fcntl_module

RS = b'\x1e'   # Record Separator — record terminator
US = b'\x1f'   # Unit Separator  — name/value delimiter within record

def read_until(fobj, delim):
    """Read bytes from fobj until delim byte is seen. Returns bytes without delim, or None on EOF."""
    buf = b''
    while True:
        ch = fobj.read(1)
        if not ch:
            return None
        if ch == delim:
            return buf
        buf += ch

def read_record(fobj):
    """Read one KIND RS body RS record.
    Returns (kind_str, name_str, value_str) or None on EOF."""
    kind_b = read_until(fobj, RS)
    if kind_b is None:
        return None
    body_b = read_until(fobj, RS)
    if body_b is None:
        return None
    if US in body_b:
        name_b, value_b = body_b.split(US, 1)
    else:
        name_b, value_b = body_b, b''
    return (kind_b.decode('utf-8', errors='replace'),
            name_b.decode('utf-8', errors='replace'),
            value_b.decode('utf-8', errors='replace'))

def main():
    if len(sys.argv) < 5:
        print("Usage: monitor_sync.py <timeout> <names> <ready_pipes> <go_pipes>")
        sys.exit(2)

    timeout    = float(sys.argv[1])
    names      = sys.argv[2].split(',')
    evt_paths  = sys.argv[3].split(',')
    go_paths  = sys.argv[4].split(',')
    n          = len(names)

    assert len(evt_paths) == n and len(go_paths) == n

    print(f"[sync monitor] opening {n} ready pipes (read)...", flush=True)
    # Open ready pipes read-side non-blocking (no writer yet — that's ok)
    evt_fds = []
    for p in evt_paths:
        fd = os.open(p, os.O_RDONLY | os.O_NONBLOCK)
        flags = fcntl_module.fcntl(fd, fcntl_module.F_GETFL)
        fcntl_module.fcntl(fd, fcntl_module.F_SETFL, flags & ~os.O_NONBLOCK)
        evt_fds.append(os.fdopen(fd, 'rb', buffering=0))  # binary, unbuffered

    print(f"[sync monitor] opening {n} go pipes (write)...", flush=True)
    ack_fds = []
    for p in go_paths:
        # O_RDWR: opens immediately without blocking even if no reader yet.
        # This is the canonical solution for the FIFO bidirectional open deadlock.
        # (O_WRONLY|O_NONBLOCK fails with ENXIO if no reader; O_RDWR succeeds always.)
        fd = os.open(p, os.O_RDWR)
        ack_fds.append(fd)

    print(f"[sync monitor] all named pipes open — signalling ready", flush=True)
    # Signal ready to run_monitor_sync.sh via stdout line
    print("READY", flush=True)

    step      = 0
    alive     = list(range(n))  # indices of still-running participants
    bufs      = [''] * n        # line buffers per participant
    done      = [False] * n     # EOF seen

    while True:
        # Collect one complete line from each alive participant
        events = [None] * n
        deadline = time.monotonic() + timeout

        remaining = list(alive)
        while remaining:
            now = time.monotonic()
            if now >= deadline:
                for i in remaining:
                    print(f"TIMEOUT [{names[i]}] at step {step} — last event seen: {events[i]!r}")
                # Send STOP to everyone still waiting
                for i in alive:
                    if events[i] is not None:
                        try: os.write(ack_fds[i], b'S')
                        except: pass
                sys.exit(2)

            readable, _, _ = select.select(
                [evt_fds[i] for i in remaining], [], [],
                deadline - now)

            for fobj in readable:
                i = evt_fds.index(fobj)
                rec = read_record(fobj)
                if rec is None:
                    # EOF — participant exited
                    done[i] = True
                    remaining.remove(i)
                    events[i] = '__EOF__'
                else:
                    kind, name, value = rec
                    # Canonical event key: "KIND\x1fNAME\x1fVALUE"
                    # Normalize name to uppercase: SPITBOL lowercases variable
                    # names; CSNOBOL4 preserves case. Compare uppercase always.
                    events[i] = f'{kind}\x1f{name.upper()}\x1f{value}'
                    remaining.remove(i)

        step += 1

        # Check: all EOF → done
        if all(done[i] or events[i] == '__EOF__' for i in alive):
            print(f"PASS — all {n} participants reached END after {step} steps")
            sys.exit(0)

        # Oracle is participant 0 (csn)
        oracle_event = events[0]

        diverged = []
        for i in alive:
            if events[i] != oracle_event and events[i] != '__EOF__':
                diverged.append(i)

        if diverged:
            print(f"\nDIVERGENCE at step {step}:")
            def fmt(e):
                if e == '__EOF__': return '<EOF>'
                parts = e.split('\x1f', 2)
                if len(parts) == 3:
                    return f'{parts[0]} {parts[1]} = {parts[2]!r}'
                return repr(e)
            print(f"  oracle [{names[0]}]: {fmt(oracle_event)}")
            for i in diverged:
                print(f"  FAIL   [{names[i]}]: {fmt(events[i])}")
            agree = [i for i in alive if i not in diverged and i != 0]
            if agree:
                print(f"  AGREE  [{','.join(names[i] for i in agree)}]: {oracle_event!r}")
            # Send STOP to all
            for i in alive:
                try: os.write(ack_fds[i], b'S')
                except: pass
            sys.exit(1)

        # All agree — send GO to all alive
        for i in alive:
            if not done[i]:
                try: os.write(ack_fds[i], b'G')
                except: pass

        # Remove finished participants
        alive = [i for i in alive if not done[i]]
        if not alive:
            print(f"PASS — all participants finished cleanly after {step} steps")
            sys.exit(0)

if __name__ == '__main__':
    main()
