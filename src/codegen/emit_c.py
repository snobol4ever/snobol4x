"""
emit_c.py — SNOBOL4-tiny C-with-gotos code generator  (Sprint 6 revision)

Sprint 6 adds the function-per-pattern model needed for Ref / mutual recursion.
When the graph contains NO Ref nodes the original flat-goto path is used
unchanged (Sprints 0-5 backward compatibility).

Function-per-pattern convention (mirrors test_sno_2.c / test_sno_3.c gold std):
    str_t NAME(NAME_t **zz, int entry)
    entry 0 = alpha (first call, allocate frame)
    entry 1 = beta  (backtrack)
    return: SNO_EMPTY (.ptr==0) = failure; anything else = matched span

All match state is global: Sigma (subject), Omega (length), Delta (cursor).
"""

from ir import (Graph, Node, Lit, Any as IrAny, Span, Break, Notany,
                Len, Pos, Rpos, Arb, Arbno, Alt, Cat, Assign, Print, Ref)


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _has_ref(node):
    if isinstance(node, Ref): return True
    if isinstance(node, (Alt, Cat)):    return _has_ref(node.left) or _has_ref(node.right)
    if isinstance(node, (Arbno, Assign)): return _has_ref(node.child)
    return False

def _graph_has_ref(graph):
    return any(_has_ref(graph.get(n)) for n in graph.names())


# ---------------------------------------------------------------------------
# FlatEmitter — original model (Sprints 0-5, no Ref)
# ---------------------------------------------------------------------------

class FlatEmitter:
    def __init__(self, graph):
        self.graph   = graph
        self.lines   = []
        self.statics = []
        self.counter = 0

    def fresh(self, p):
        self.counter += 1
        return f"{p}{self.counter}"

    def L(self, s):  self.lines.append(s)
    def S(self, s):  self.statics.append(s)

    def emit_node(self, node, nid, gamma, omega):
        if isinstance(node, Lit):
            n    = len(node.s.encode())
            safe = node.s.replace('\\','\\\\').replace('"','\\"')
            self.S(f"static int64_t {nid}_saved_cursor;")
            self.L(f"{nid}_alpha:")
            self.L(f"    if (cursor + {n} > subject_len) goto {omega};")
            self.L(f'    if (memcmp(subject + cursor, "{safe}", {n}) != 0) goto {omega};')
            self.L(f"    {nid}_saved_cursor = cursor;  cursor += {n};  goto {gamma};")
            self.L(f"{nid}_beta:  cursor = {nid}_saved_cursor;  goto {omega};")

        elif isinstance(node, Pos):
            self.L(f"{nid}_alpha:  if (cursor != {node.n}) goto {omega};  goto {gamma};")
            self.L(f"{nid}_beta:   goto {omega};")

        elif isinstance(node, Rpos):
            self.L(f"{nid}_alpha:  if (cursor != subject_len - {node.n}) goto {omega};  goto {gamma};")
            self.L(f"{nid}_beta:   goto {omega};")

        elif isinstance(node, Len):
            self.S(f"static int64_t {nid}_saved_cursor;")
            self.L(f"{nid}_alpha:")
            self.L(f"    if (cursor + {node.n} > subject_len) goto {omega};")
            self.L(f"    {nid}_saved_cursor = cursor;  cursor += {node.n};  goto {gamma};")
            self.L(f"{nid}_beta:  cursor = {nid}_saved_cursor;  goto {omega};")

        elif isinstance(node, Span):
            safe = node.charset.replace('\\','\\\\').replace('"','\\"')
            self.S(f"static int64_t {nid}_saved_cursor;")
            self.L(f"{nid}_alpha: {{")
            self.L(f'    const char *cs = "{safe}"; int64_t st = cursor;')
            self.L(f"    while (cursor < subject_len && strchr(cs, subject[cursor])) cursor++;")
            self.L(f"    if (cursor == st) goto {omega};")
            self.L(f"    {nid}_saved_cursor = st; }}")
            self.L(f"    goto {gamma};")
            self.L(f"{nid}_beta:")
            self.L(f"    if (cursor <= {nid}_saved_cursor + 1) {{ cursor = {nid}_saved_cursor; goto {omega}; }}")
            self.L(f"    cursor--;  goto {gamma};")

        elif isinstance(node, IrAny):
            safe = node.charset.replace('\\','\\\\').replace('"','\\"').replace("'","\\'")
            self.S(f"static int64_t {nid}_saved_cursor;")
            self.L(f"{nid}_alpha:  if (cursor >= subject_len) goto {omega};")
            self.L(f'    if (!strchr("{safe}", subject[cursor])) goto {omega};')
            self.L(f"    {nid}_saved_cursor = cursor;  cursor += 1;  goto {gamma};")
            self.L(f"{nid}_beta:  cursor = {nid}_saved_cursor;  goto {omega};")

        elif isinstance(node, Notany):
            safe = node.charset.replace('\\','\\\\').replace('"','\\"').replace("'","\\'")
            self.S(f"static int64_t {nid}_saved_cursor;")
            self.L(f"{nid}_alpha:  if (cursor >= subject_len) goto {omega};")
            self.L(f'    if (strchr("{safe}", subject[cursor])) goto {omega};')
            self.L(f"    {nid}_saved_cursor = cursor;  cursor += 1;  goto {gamma};")
            self.L(f"{nid}_beta:  cursor = {nid}_saved_cursor;  goto {omega};")

        elif isinstance(node, Break):
            safe = node.charset.replace('\\','\\\\').replace('"','\\"').replace("'","\\'")
            self.S(f"static int64_t {nid}_saved_cursor;")
            self.S(f"static int64_t {nid}_delta;")
            self.L(f"{nid}_alpha: {{")
            self.L(f'    const char *cs = "{safe}";')
            self.L(f"    {nid}_delta = 0;")
            self.L(f"    while (cursor + {nid}_delta < subject_len && !strchr(cs, subject[cursor + {nid}_delta]))")
            self.L(f"        {nid}_delta++;")
            self.L(f"    if (cursor + {nid}_delta >= subject_len) goto {omega};")
            self.L(f"    {nid}_saved_cursor = cursor;  cursor += {nid}_delta;  }}")
            self.L(f"    goto {gamma};")
            self.L(f"{nid}_beta:  cursor = {nid}_saved_cursor;  goto {omega};")

        elif isinstance(node, Cat):
            li = self.fresh("cat_l"); ri = self.fresh("cat_r")
            self.L(f"{nid}_alpha:  goto {li}_alpha;")
            self.emit_node(node.left,  li, gamma=f"{ri}_alpha",  omega=f"{nid}_beta")
            self.emit_node(node.right, ri, gamma=gamma,           omega=f"{li}_beta")
            self.L(f"{nid}_beta:  goto {omega};")

        elif isinstance(node, Alt):
            li = self.fresh("alt_l"); ri = self.fresh("alt_r")
            self.L(f"{nid}_alpha:  goto {li}_alpha;")
            self.emit_node(node.left,  li, gamma=gamma, omega=f"{ri}_alpha")
            self.emit_node(node.right, ri, gamma=gamma, omega=omega)
            self.L(f"{nid}_beta:  goto {ri}_beta;")

        elif isinstance(node, Assign):
            ci      = self.fresh("assign_c")
            var_up  = node.var.upper()
            self.S(f"static str_t var_{var_up};")
            self.S(f"static int64_t {nid}_start;")
            self.L(f"{nid}_alpha:  {nid}_start = cursor;  goto {ci}_alpha;")
            self.emit_node(node.child, ci, gamma=f"{nid}_ok", omega=omega)
            self.L(f"{nid}_ok:")
            self.L(f"    var_{var_up}.ptr = subject + {nid}_start;")
            self.L(f"    var_{var_up}.len = cursor - {nid}_start;")
            if var_up == "OUTPUT":
                self.L(f"    sno_output(var_{var_up});")
            self.L(f"    goto {gamma};")
            self.L(f"{nid}_beta:  goto {ci}_beta;")

        elif isinstance(node, Arb):
            self.S(f"static int64_t {nid}_start;")
            self.S(f"static int64_t {nid}_depth;")
            self.L(f"{nid}_alpha:")
            self.L(f"    {nid}_start = cursor;  {nid}_depth = 0;  goto {gamma};")
            self.L(f"{nid}_beta:")
            self.L(f"    {nid}_depth++;")
            self.L(f"    if ({nid}_start + {nid}_depth > subject_len) goto {omega};")
            self.L(f"    cursor = {nid}_start + {nid}_depth;  goto {gamma};")

        elif isinstance(node, Arbno):
            ci = self.fresh("arbno_c")
            self.S(f"static int64_t {nid}_cursors[64];")
            self.S(f"static int     {nid}_depth;")
            self.L(f"{nid}_alpha:  {nid}_depth = -1;  goto {gamma};")
            self.L(f"{nid}_beta:")
            self.L(f"    {nid}_depth++;")
            self.L(f"    if ({nid}_depth >= 64) goto {omega};")
            self.L(f"    {nid}_cursors[{nid}_depth] = cursor;")
            self.L(f"    goto {ci}_alpha;")
            self.emit_node(node.child, ci,
                           gamma=f"{nid}_child_ok", omega=f"{nid}_child_fail")
            self.L(f"{nid}_child_ok:   goto {gamma};")
            self.L(f"{nid}_child_fail:")
            self.L(f"    cursor = {nid}_cursors[{nid}_depth];  {nid}_depth--;")
            self.L(f"    goto {omega};")

        elif isinstance(node, Print):
            safe = node.expr.replace('\\','\\\\').replace('"','\\"')
            self.L(f"{nid}_alpha:")
            self.L(f'    sno_output_cstr("{safe}");')
            self.L(f"    goto {gamma};")
            self.L(f"{nid}_beta:  goto {omega};")

        elif isinstance(node, Ref):
            self.L(f"{nid}_alpha:  goto {node.name}_alpha;")
            self.L(f"{nid}_beta:   goto {node.name}_beta;")

        else:
            self.L(f"/* TODO {type(node).__name__} */")
            self.L(f"{nid}_alpha: goto {omega};  {nid}_beta: goto {omega};")


# ---------------------------------------------------------------------------
# FuncEmitter — function-per-pattern model (Sprint 6+)
# ---------------------------------------------------------------------------

class FuncEmitter:
    ALPHA = 0
    BETA  = 1

    def __init__(self, graph):
        self.graph    = graph
        self.counter  = 0
        self.fields   = {}   # name -> list[str]
        self.body     = {}   # name -> list[str]
        self.var_decls = set()

    def fresh(self, p):
        self.counter += 1
        return f"{p}{self.counter}"

    def F(self, pat, s):  self.fields.setdefault(pat, []).append(s)
    def L(self, pat, s):  self.body.setdefault(pat, []).append(s)

    def emit_node(self, pat, node, nid, gamma, omega):
        L = lambda s: self.L(pat, s)

        if isinstance(node, Alt):
            li = self.fresh("al"); ri = self.fresh("ar")
            self.F(pat, f"    int {nid}_i;")
            L(f"{nid}_alpha:")
            L(f"    z->{nid}_i = 1;  goto {li}_alpha;")
            self.emit_node(pat, node.left,  li,
                           gamma=f"{nid}_lg", omega=f"{nid}_lo")
            L(f"{nid}_lg:  z->{nid}_i = 1;  goto {gamma};")
            L(f"{nid}_lo:  z->{nid}_i = 2;  goto {ri}_alpha;")
            self.emit_node(pat, node.right, ri,
                           gamma=f"{nid}_rg", omega=omega)
            L(f"{nid}_rg:  z->{nid}_i = 2;  goto {gamma};")
            L(f"{nid}_beta:")
            L(f"    if (z->{nid}_i == 1) goto {li}_beta;")
            L(f"    if (z->{nid}_i == 2) goto {ri}_beta;")
            L(f"    goto {omega};")

        elif isinstance(node, Lit):
            n    = len(node.s.encode())
            safe = node.s.replace('\\','\\\\').replace('"','\\"')
            self.F(pat, f"    int64_t {nid}_saved;")
            L(f"{nid}_alpha:")
            L(f"    if (Delta + {n} > Omega) goto {omega};")
            L(f'    if (memcmp(Sigma + Delta, "{safe}", {n}) != 0) goto {omega};')
            L(f"    z->{nid}_saved = Delta;  Delta += {n};  goto {gamma};")
            L(f"{nid}_beta:  Delta = z->{nid}_saved;  goto {omega};")

        elif isinstance(node, Pos):
            L(f"{nid}_alpha:  if (Delta != {node.n}) goto {omega};  goto {gamma};")
            L(f"{nid}_beta:   goto {omega};")

        elif isinstance(node, Rpos):
            L(f"{nid}_alpha:  if (Delta != Omega - {node.n}) goto {omega};  goto {gamma};")
            L(f"{nid}_beta:   goto {omega};")

        elif isinstance(node, Len):
            self.F(pat, f"    int64_t {nid}_saved;")
            L(f"{nid}_alpha:")
            L(f"    if (Delta + {node.n} > Omega) goto {omega};")
            L(f"    z->{nid}_saved = Delta;  Delta += {node.n};  goto {gamma};")
            L(f"{nid}_beta:  Delta = z->{nid}_saved;  goto {omega};")

        elif isinstance(node, Span):
            safe = node.charset.replace('\\','\\\\').replace('"','\\"')
            self.F(pat, f"    int64_t {nid}_saved;")
            L(f"{nid}_alpha: {{")
            L(f'    const char *cs = "{safe}"; int64_t st = Delta;')
            L(f"    while (Delta < Omega && strchr(cs, Sigma[Delta])) Delta++;")
            L(f"    if (Delta == st) goto {omega};")
            L(f"    z->{nid}_saved = st; }}")
            L(f"    goto {gamma};")
            L(f"{nid}_beta:")
            L(f"    if (Delta <= z->{nid}_saved + 1) {{ Delta = z->{nid}_saved; goto {omega}; }}")
            L(f"    Delta--;  goto {gamma};")

        elif isinstance(node, Cat):
            li = self.fresh("cl"); ri = self.fresh("cr")
            self.F(pat, f"    int {nid}_entered;")
            L(f"{nid}_alpha:  z->{nid}_entered = 0;  goto {li}_alpha;")
            self.emit_node(pat, node.left,  li,
                           gamma=f"{nid}_li_ok", omega=f"{nid}_beta")
            L(f"{nid}_li_ok:")
            L(f"    z->{nid}_entered = 1;  goto {ri}_alpha;")
            self.emit_node(pat, node.right, ri, gamma=gamma, omega=f"{nid}_ri_fail")
            # Right child failed: clear entered, backtrack left
            L(f"{nid}_ri_fail:")
            L(f"    z->{nid}_entered = 0;  goto {li}_beta;")
            # Cat beta: retry right if entered; otherwise backtrack left
            L(f"{nid}_beta:")
            L(f"    if (z->{nid}_entered) goto {ri}_beta;")
            L(f"    goto {omega};")

        elif isinstance(node, Assign):
            ci     = self.fresh("ac")
            var_up = node.var.upper()
            if var_up != "OUTPUT":
                self.var_decls.add(f"static str_t var_{var_up};")
            self.F(pat, f"    int64_t {nid}_start;")
            L(f"{nid}_alpha:  z->{nid}_start = Delta;  goto {ci}_alpha;")
            self.emit_node(pat, node.child, ci, gamma=f"{nid}_ok", omega=omega)
            L(f"{nid}_ok: {{")
            L(f"    str_t _v = {{ Sigma + z->{nid}_start, Delta - z->{nid}_start }};")
            if var_up == "OUTPUT":
                L(f"    sno_output(_v);")
            else:
                L(f"    var_{var_up} = _v;")
            L(f"    }} goto {gamma};")
            L(f"{nid}_beta:  goto {ci}_beta;")

        elif isinstance(node, Arb):
            self.F(pat, f"    int64_t {nid}_start;")
            self.F(pat, f"    int64_t {nid}_depth;")
            L(f"{nid}_alpha:")
            L(f"    z->{nid}_start = Delta;  z->{nid}_depth = 0;  goto {gamma};")
            L(f"{nid}_beta:")
            L(f"    z->{nid}_depth++;")
            L(f"    if (z->{nid}_start + z->{nid}_depth > Omega) goto {omega};")
            L(f"    Delta = z->{nid}_start + z->{nid}_depth;  goto {gamma};")

        elif isinstance(node, Arbno):
            ci = self.fresh("an")
            self.F(pat, f"    int64_t {nid}_cursors[64];")
            self.F(pat, f"    int     {nid}_depth;")
            L(f"{nid}_alpha:  z->{nid}_depth = -1;  goto {gamma};")
            L(f"{nid}_beta:")
            L(f"    z->{nid}_depth++;")
            L(f"    if (z->{nid}_depth >= 64) goto {omega};")
            L(f"    z->{nid}_cursors[z->{nid}_depth] = Delta;")
            L(f"    goto {ci}_alpha;")
            self.emit_node(pat, node.child, ci,
                           gamma=f"{nid}_cok", omega=f"{nid}_cfail")
            L(f"{nid}_cok:   goto {gamma};")
            L(f"{nid}_cfail:")
            L(f"    Delta = z->{nid}_cursors[z->{nid}_depth];  z->{nid}_depth--;")
            L(f"    goto {omega};")

        elif isinstance(node, Ref):
            # ── The Sprint 6 mechanism ────────────────────────────────────────
            # Each Ref call site gets its own child frame pointer in the parent
            # struct, named <nid>_z.  We allocate it on alpha (sno_enter sets
            # frame to NULL so the callee allocates fresh), and reuse on beta.
            target = node.name
            fld    = f"{nid}_z"
            self.F(pat, f"    {target}_t *{fld};")
            L(f"{nid}_alpha:  z->{fld} = 0;")
            L(f"    {{ str_t _r = {target}(&z->{fld}, {self.ALPHA});")
            L(f"      if (_r.ptr == 0) goto {omega};")
            L(f"      goto {gamma}; }}")
            L(f"{nid}_beta:")
            L(f"    {{ str_t _r = {target}(&z->{fld}, {self.BETA});")
            L(f"      if (_r.ptr == 0) goto {omega};")
            L(f"      goto {gamma}; }}")

        else:
            L(f"/* TODO {type(node).__name__} */")
            L(f"{nid}_alpha: goto {omega};  {nid}_beta: goto {omega};")

    # -----------------------------------------------------------------------
    # Proebsting optimization pass
    # "Simple Translation of Goal-Directed Evaluation" — Todd A. Proebsting
    #
    # The four-port template expansion produces correct but noisy code:
    # many labels whose entire block is a bare "goto X;" — branches to
    # branches.  Two passes clean this up entirely:
    #
    #   1. Copy propagation  — find every label L where the only statement
    #      in L's block is "goto T;".  Replace every "goto L;" with "goto T;"
    #      Repeat until stable (chains collapse; two passes handles cycles).
    #
    #   2. Dead-label elimination — after propagation, any label that has
    #      zero incoming gotos AND is not a protected entry label is
    #      unreachable; remove it and its block.
    #
    # Result closely resembles hand-written code (Proebsting Fig 1 → Fig 2).
    # -----------------------------------------------------------------------

    def _optimize_body(self, pat, entry_labels):
        """Run copy-propagation + dead-label elimination on self.body[pat]."""
        import re as _re

        lines = self.body.get(pat, [])
        if not lines:
            return

        # ── Step 0: parse lines into blocks ─────────────────────────────
        label_re = _re.compile(r'^(\w+):(.*)$')
        blocks = []   # list of [label_or_None, [stmts]]
        for raw in lines:
            line = raw.strip()
            m = label_re.match(line)
            if m:
                lbl  = m.group(1)
                rest = m.group(2).strip()
                blocks.append([lbl, [rest] if rest else []])
            else:
                if not blocks:
                    blocks.append([None, []])
                blocks[-1][1].append(line)

        # ── Step 1: label index ──────────────────────────────────────────
        lbl_to_block = {b[0]: b for b in blocks if b[0]}

        # ── Step 2: copy propagation ─────────────────────────────────────
        goto_re = _re.compile(r'^goto\s+(\w+)\s*;$')

        def resolve(lbl, seen=None):
            if seen is None: seen = set()
            if lbl in seen:  return lbl
            seen.add(lbl)
            b = lbl_to_block.get(lbl)
            if b is None: return lbl
            stmts = [s for s in b[1] if s]
            if len(stmts) == 1:
                gm = goto_re.match(stmts[0])
                if gm:
                    return resolve(gm.group(1), seen)
            return lbl

        redirect = {}
        for lbl, b in lbl_to_block.items():
            stmts = [s for s in b[1] if s]
            if len(stmts) == 1 and goto_re.match(stmts[0]):
                final = resolve(lbl)
                if final != lbl:
                    redirect[lbl] = final

        def rewrite_stmt(stmt):
            def sub(m2):
                t = m2.group(1)
                return f"goto {redirect[t]};" if t in redirect else m2.group(0)
            return _re.sub(r'\bgoto\s+(\w+)\s*;', sub, stmt)

        for b in blocks:
            b[1] = [rewrite_stmt(s) for s in b[1]]

        # ── Step 3: dead-label elimination ───────────────────────────────
        incoming = {b[0]: 0 for b in blocks if b[0]}
        for el in entry_labels:
            if el in incoming:
                incoming[el] = 999
        for final in redirect.values():
            if final in incoming:
                incoming[final] = max(incoming[final], 1)
        all_text = "\n".join(s for b in blocks for s in b[1])
        for m in _re.finditer(r'\bgoto\s+(\w+)\s*;', all_text):
            t = m.group(1)
            if t in incoming:
                incoming[t] += 1

        live_blocks = []
        for b in blocks:
            lbl = b[0]
            if lbl and incoming.get(lbl, 0) == 0 and lbl not in entry_labels:
                stmts = [s for s in b[1] if s]
                if len(stmts) <= 1:
                    continue
            live_blocks.append(b)

        # ── Step 4: re-serialise ─────────────────────────────────────────
        out = []
        for lbl, stmts in live_blocks:
            non_empty = [s for s in stmts if s]
            if lbl:
                if len(non_empty) == 1:
                    out.append(f"{lbl}:  {non_empty[0]}")
                elif non_empty:
                    out.append(f"{lbl}:")
                    for s in non_empty:
                        out.append(f"    {s}")
                else:
                    out.append(f"{lbl}:")
            else:
                for s in non_empty:
                    out.append(s)

        self.body[pat] = out

    def generate_source(self, root_name, subject, include_main):
        names = self.graph.names()

        # populate fields + body for every pattern
        for name in names:
            self.fields.setdefault(name, [])
            self.body.setdefault(name, [])
            self.emit_node(name, self.graph.get(name),
                           nid=name,
                           gamma=f"{name}_match_ok",
                           omega=f"{name}_match_fail")

        # Proebsting optimization: copy-propagation + dead-label elimination
        for name in names:
            self._optimize_body(name, entry_labels={
                f"{name}_alpha", f"{name}_beta",
                f"{name}_match_ok", f"{name}_match_fail"
            })

        out = []
        out.append("/* Generated by SNOBOL4-tiny emit_c.py — DO NOT EDIT */")
        out.append("#include <stdint.h>")
        out.append("#include <string.h>")
        out.append("#include <stdio.h>")
        out.append('#include "../../src/runtime/runtime.h"')
        out.append("")
        out.append("/* === global match state === */")
        out.append("static const char *Sigma = 0;")
        out.append("static int64_t     Omega = 0;")
        out.append("static int64_t     Delta = 0;")
        out.append("static const str_t SNO_EMPTY = {0, 0};")
        out.append("static inline int64_t _slen(const char *s) { int64_t n=0; while(*s++) n++; return n; }")
        out.append("")

        for d in sorted(self.var_decls):
            out.append(d)
        if self.var_decls:
            out.append("")

        # forward typedef + function declarations (required for cycles)
        out.append("/* === forward declarations === */")
        for name in names:
            out.append(f"typedef struct _{name} {name}_t;")
        out.append("")
        for name in names:
            out.append(f"static str_t {name}({name}_t **, int);")
        out.append("")

        # one struct + function per pattern
        for name in names:
            flds = self.fields.get(name, [])
            bdy  = self.body.get(name, [])

            out.append(f"struct _{name} {{")
            if flds:
                for f in flds:
                    out.append(f)
            else:
                out.append("    int _dummy;")
            out.append("};")
            out.append("")

            out.append(f"static str_t {name}({name}_t **zz, int entry) {{")
            out.append(f"    {name}_t *z = *zz;")
            out.append( "    if (entry == 0) {")
            out.append(f"        z = ({name}_t *)sno_enter((void **)zz, sizeof({name}_t));")
            out.append(f"        goto {name}_alpha;")
            out.append( "    }")
            out.append( "    if (!z) return SNO_EMPTY;  /* no frame = nothing to backtrack */")
            out.append(f"    goto {name}_beta;")
            out.append("")
            for line in bdy:
                out.append("    " + line)
            out.append(f"    {name}_match_ok:   return (str_t){{ Sigma, Delta }};")
            out.append(f"    {name}_match_fail: sno_exit((void **)zz); return SNO_EMPTY;")
            out.append("}")
            out.append("")

        if include_main:
            safe = subject.replace('\\','\\\\').replace('"','\\"')
            out.append("int main(void) {")
            out.append(f'    Sigma = "{safe}";')
            out.append( "    Omega = _slen(Sigma);")
            out.append( "    Delta = 0;")
            out.append(f"    {root_name}_t *frame = 0;")
            out.append( "    sno_arena_reset();  /* fresh arena for each top-level match */")
            out.append( "    int first = 1;")
            out.append( "    int64_t prev_delta = -1;")
            out.append( "    while (1) {")
            out.append(f"        str_t r = {root_name}(&frame, first ? 0 : 1);")
            out.append( "        first = 0;")
            out.append( "        if (r.ptr == 0) { sno_output_cstr(\"Failure.\"); return 1; }")
            out.append( "        if (Delta == Omega) { sno_output_cstr(\"Success!\"); return 0; }")
            out.append( "        /* no progress — beta is stuck, all alternatives exhausted */")
            out.append( "        if (Delta == prev_delta) { sno_output_cstr(\"Failure.\"); return 1; }")
            out.append( "        prev_delta = Delta;")
            out.append( "    }")
            out.append("}")

        return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# public API
# ---------------------------------------------------------------------------

def emit_program(graph, root_name, subject="", include_main=True):
    """Emit a complete compilable C file for the given pattern graph."""
    if _graph_has_ref(graph):
        fe = FuncEmitter(graph)
        return fe.generate_source(root_name, subject, include_main)
    else:
        return _emit_flat_program(graph, root_name, subject, include_main)


def _emit_flat_program(graph, root_name, subject, include_main):
    em = FlatEmitter(graph)
    for name in graph.names():
        node = graph.get(name)
        em.L(f"\n/* ===== pattern: {name} ===== */")
        em.emit_node(node, nid=name,
                     gamma=f"{name}_MATCH_SUCCESS",
                     omega=f"{name}_MATCH_FAIL")
        em.L(f'{name}_MATCH_SUCCESS: sno_output_cstr("Success!"); return 0;')
        em.L(f'{name}_MATCH_FAIL:    sno_output_cstr("Failure."); return 1;')

    out = []
    out.append("/* Generated by SNOBOL4-tiny emit_c.py — DO NOT EDIT */")
    out.append("#include <stdint.h>")
    out.append("#include <string.h>")
    out.append("#include <stdio.h>")
    out.append('#include "../../src/runtime/runtime.h"')
    out.append("")
    out.append("/* === static storage === */")
    for s in em.statics: out.append(s)
    out.append("")

    if include_main:
        safe = subject.replace('\\','\\\\').replace('"','\\"')
        out.append("int main(void) {")
        out.append(f'    const char *subject    = "{safe}";')
        out.append(f"    int64_t     subject_len = {len(subject.encode())};")
        out.append( "    int64_t     cursor      = 0;")
        out.append( "    (void)cursor; (void)subject; (void)subject_len;")
        out.append("")
        out.append(f"    goto {root_name}_alpha;")
        out.append("")

    for line in em.lines:
        out.append("    " + line if include_main else line)

    if include_main:
        out.append("}")

    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# smoke test
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    from ir import Graph, Cat, Pos, Rpos, Lit, Alt, Ref

    print("/* ===== FLAT (no Ref) ===== */")
    g1 = Graph()
    g1.add("root", Cat(Pos(0), Cat(Lit("hello"), Rpos(0))))
    print(emit_program(g1, "root", subject="hello"))

    print("/* ===== FUNC (EVEN/ODD mutual recursion) ===== */")
    g2 = Graph()
    g2.add("EVEN", Alt(Lit(""), Cat(Lit("x"), Ref("ODD"))))
    g2.add("ODD",  Cat(Lit("x"), Ref("EVEN")))
    print(emit_program(g2, "EVEN", subject="xxxx"))
