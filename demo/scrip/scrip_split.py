#!/usr/bin/env python3
"""scrip_split.py — split a SCRIP polyglot source file into per-language files.

Usage: python3 scrip_split.py INPUT.md OUTDIR

Writes one file per fenced block:
  OUTDIR/snobol4.sno
  OUTDIR/icon.icn
  OUTDIR/prolog.pro

Prints a manifest line per block:  LANG CLASSNAME FILENAME
Exit 0 on success, 1 on error.
"""

import re
import sys
import os

FENCE_OPEN  = re.compile(r'^```(\w+)\s*$', re.IGNORECASE)
FENCE_CLOSE = re.compile(r'^```\s*$')

LANG_MAP = {
    'snobol4': ('snobol4', 'sno'),
    'icon':    ('icon',    'icn'),
    'prolog':  ('prolog',  'pro'),
}

CLASS_MAP = {
    'snobol4': 'DemoSnobol4',
    'icon':    'DemoIcon',
    'prolog':  'DemoProlog',
}

def split(src, outdir):
    os.makedirs(outdir, exist_ok=True)
    blocks = {}   # lang -> [lines]
    current = None
    with open(src) as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if current is None:
                m = FENCE_OPEN.match(line)
                if m:
                    lang = m.group(1).lower()
                    if lang not in LANG_MAP:
                        print(f"WARNING line {lineno}: unknown language '{lang}', skipping block",
                              file=sys.stderr)
                    else:
                        current = lang
                        if lang not in blocks:
                            blocks[lang] = []
            else:
                if FENCE_CLOSE.match(line):
                    current = None
                else:
                    blocks[current].append(line)

    if not blocks:
        print("ERROR: no fenced blocks found in " + src, file=sys.stderr)
        return 1

    for lang, lines in blocks.items():
        _, ext = LANG_MAP[lang]
        outfile = os.path.join(outdir, f"{lang}.{ext}")
        with open(outfile, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        classname = CLASS_MAP[lang]
        print(f"{lang} {classname} {outfile}")

    return 0

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} INPUT.scrip OUTDIR", file=sys.stderr)
        sys.exit(1)
    sys.exit(split(sys.argv[1], sys.argv[2]))
