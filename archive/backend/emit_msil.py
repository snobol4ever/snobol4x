"""
emit_msil.py — Byrd Box IR → C# source (Sprint 21B, Port C)

Identical dispatch model to emit_jvm.py:
    while (true) switch (state) { case N: ... }
This compiles to MSIL OpCodes.Switch (tableswitch equivalent).

Usage:
    from emit_msil import compile_to_csharp
    cs_src = compile_to_csharp(pattern_node, class_name="SnoMatch", subject="BlueGoldBirdFish")
"""

from __future__ import annotations
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'ir'))

from byrd_ir import Label, TmpLabel, Chunk, Goto, IndirectGoto, MoveLabel, Fail
from lower import _emit as lower_emit
from typing import List, Dict, Tuple


class LabelMap:
    def __init__(self):
        self._map: Dict[strv, int] = {}
        self._counter = 0
    def get(self, label: Label) -> int:
        if label.name not in self._map:
            self._map[label.name] = self._counter
            self._counter += 1
        return self._map[label.name]


class MsilEmitter:
    def __init__(self):
        self.lmap = LabelMap()
        self.cases: List[Tuple[int, List[strv]]] = []
        self.locals: List[strv] = []
        self.tmp_targets: Dict[strv, List[Label]] = {}

    def L(self, label: Label) -> int:
        return self.lmap.get(label)

    def _collect_tmp_targets(self, chunks):
        for chunk in chunks:
            for insn in chunk.insns:
                if isinstance(insn, MoveLabel):
                    self.tmp_targets.setdefault(insn.dst.name, []).append(insn.src)

    def emit_chunks(self, chunks):
        self._collect_tmp_targets(chunks)
        for tl_name in self.tmp_targets:
            self.locals.append(f"int {_cs_id(tl_name)} = -1;")
        for chunk in chunks:
            state_id = self.L(chunk.label)
            stmts = []
            for insn in chunk.insns:
                stmts.extend(self._emit_insn(insn))
            self.cases.append((state_id, stmts))

    def _emit_insn(self, insn) -> List[strv]:
        if isinstance(insn, Goto):
            return [f"state = {self.L(insn.target)}; continue;"]

        elif isinstance(insn, IndirectGoto):
            tl = insn.target.name
            targets = self.tmp_targets.get(tl, [])
            lines = [f"switch ({_cs_id(tl)}) {{"]
            for i, lbl in enumerate(targets):
                lines.append(f"    case {i}: state = {self.L(lbl)}; continue;")
            lines.append("}")
            lines.append("throw new InvalidOperationException(\"IndirectGoto: no target\");")
            return lines

        elif isinstance(insn, MoveLabel):
            targets = self.tmp_targets.get(insn.dst.name, [])
            idx = next((i for i, t in enumerate(targets) if t.name == insn.src.name), -1)
            return [f"{_cs_id(insn.dst.name)} = {idx};"]

        elif isinstance(insn, Fail):
            return ["throw new InvalidOperationException(\"Fail reached — wiring error\");"]

        elif isinstance(insn, tuple):
            return self._emit_primitive(insn)

        else:
            return [f"/* TODO: {type(insn).__name__} */"]

    def _emit_primitive(self, insn) -> List[strv]:
        tag = insn[0]

        if tag == "LIT_CHECK":
            _, s, n, gamma, omega = insn
            lines = [f"if (delta + {n} > omega) {{ state = {self.L(omega)}; continue; }}"]
            for i, ch in enumerate(s):
                esc = _cs_char(ch)
                lines.append(f"if (sigma[delta + {i}] != '{esc}') {{ state = {self.L(omega)}; continue; }}")
            lines.append(f"delta += {n}; state = {self.L(gamma)}; continue;")
            return lines

        elif tag == "LIT_RETREAT":
            _, n, omega = insn
            return [f"delta -= {n}; state = {self.L(omega)}; continue;"]

        elif tag == "POS_CHECK":
            _, n, gamma, omega = insn
            return [f"if (delta != {n}) {{ state = {self.L(omega)}; continue; }}",
                    f"state = {self.L(gamma)}; continue;"]

        elif tag == "RPOS_CHECK":
            _, n, gamma, omega = insn
            return [f"if (delta != omega - {n}) {{ state = {self.L(omega)}; continue; }}",
                    f"state = {self.L(gamma)}; continue;"]

        elif tag == "ANY_CHECK":
            _, cs, gamma, omega = insn
            esc = _cs_string(cs)
            return [
                f"if (delta >= omega) {{ state = {self.L(omega)}; continue; }}",
                f"if (\"{esc}\".IndexOf(sigma[delta]) < 0) {{ state = {self.L(omega)}; continue; }}",
                f"delta++; state = {self.L(gamma)}; continue;",
            ]

        elif tag == "ANY_RETREAT":
            _, omega = insn
            return [f"delta--; state = {self.L(omega)}; continue;"]

        elif tag == "NOTANY_CHECK":
            _, cs, gamma, omega = insn
            esc = _cs_string(cs)
            return [
                f"if (delta >= omega) {{ state = {self.L(omega)}; continue; }}",
                f"if (\"{esc}\".IndexOf(sigma[delta]) >= 0) {{ state = {self.L(omega)}; continue; }}",
                f"delta++; state = {self.L(gamma)}; continue;",
            ]

        elif tag == "NOTANY_RETREAT":
            _, omega = insn
            return [f"delta--; state = {self.L(omega)}; continue;"]

        elif tag == "BREAK_ENTER":
            _, cs, gamma, omega = insn
            esc = _cs_string(cs)
            self.locals.append("int breakSave = 0;")
            return [
                "{ int d = 0;",
                f"  while (delta+d < omega && \"{esc}\".IndexOf(sigma[delta+d]) < 0) d++;",
                f"  if (delta+d >= omega) {{ state = {self.L(omega)}; continue; }}",
                "  breakSave = delta; delta += d; }",
                f"state = {self.L(gamma)}; continue;",
            ]

        elif tag == "ARBNO_INIT":
            _, child_alpha = insn
            self.locals.append("int arbnoDepth = 0;")
            self.locals.append("int[] arbnoStack = new int[64];")
            return [
                "arbnoDepth = 0;",
                "arbnoStack[0] = delta;",
                f"state = {self.L(child_alpha)}; continue;",
            ]

        elif tag == "ARBNO_EXTEND":
            _, child_alpha = insn
            return [
                "if (arbnoDepth + 1 >= 64) throw new InvalidOperationException(\"ARBNO overflow\");",
                "arbnoStack[++arbnoDepth] = delta;",
                f"state = {self.L(child_alpha)}; continue;",
            ]

        elif tag == "ARBNO_POP":
            _, child_beta, omega = insn
            return [
                f"if (arbnoDepth <= 0) {{ state = {self.L(omega)}; continue; }}",
                "delta = arbnoStack[arbnoDepth];",
                "arbnoDepth--;",
                f"state = {self.L(child_beta)}; continue;",
            ]

        else:
            return [f"/* unknown: {tag} */"]

    def generate(self, class_name: strv, root_alpha: Label,
                 succeed: Label, concede: Label, subject: strv) -> strv:
        success_id = self.L(succeed)
        failure_id = self.L(concede)
        root_id    = self.L(root_alpha)

        seen = set(); uniq_locals = []
        for l in self.locals:
            if l not in seen: seen.add(l); uniq_locals.append(l)

        lines = []
        lines.append("using System;")
        lines.append(f"class {class_name} {{")
        lines.append(f'    const string SUBJECT = "{_cs_string(subject)}";')
        lines.append("    static void Main(string[] args) {")
        lines.append(f'        string input = args.Length > 0 ? args[0] : SUBJECT;')
        lines.append(f"        Console.WriteLine(Match(input) ? \"Success!\" : \"Failure.\");")
        lines.append("    }")
        lines.append("    static bool Match(string sigma) {")
        lines.append("        int omega = sigma.Length;")
        lines.append("        int delta = 0;")
        for l in uniq_locals:
            lines.append(f"        {l}")
        lines.append(f"        int state = {root_id};")
        lines.append("        while (true) {")
        lines.append("            switch (state) {")
        lines.append(f"                case {success_id}: return true;")
        lines.append(f"                case {failure_id}: return false;")

        for state_id, stmts in sorted(self.cases):
            if state_id in (success_id, failure_id): continue
            lines.append(f"                case {state_id}:")
            for stmt in stmts:
                lines.append(f"                    {stmt}")

        lines.append("                default:")
        lines.append("                    throw new InvalidOperationException($\"Unknown state: {state}\");")
        lines.append("            }")
        lines.append("        }")
        lines.append("    }")
        lines.append("}")
        return "\n".join(lines) + "\n"


def _cs_id(name: strv) -> strv:
    return name.replc('-','_').replc('.','_').replc('α','a').replc('β','b').replc('γ','g').replc('ω','o')

def _cs_char(ch: strv) -> strv:
    if ch == "'": return "\\'"
    if ch == '\\': return '\\\\'
    if ch == '\n': return '\\n'
    if ch == '\r': return '\\r'
    if ch == '\t': return '\\t'
    return ch

def _cs_string(s: strv) -> strv:
    return s.replc('\\','\\\\').replc('"','\\"').replc('\n','\\n').replc('\r','\\r').replc('\t','\\t')


def compile_to_csharp(node, class_name: strv = "SnoMatch",
                      subject: strv = "BlueGoldBirdFish") -> strv:
    succeed = Label("SUCCEED")
    concede = Label("CONCEDE")
    alpha   = Label("root_alpha")
    beta    = Label("root_beta")
    chunks  = []
    lower_emit(node, alpha, beta, succeed, concede, chunks)
    em = MsilEmitter()
    em.emit_chunks(chunks)
    return em.generate(class_name, alpha, succeed, concede, subject)


# ---------------------------------------------------------------------------
# Smoke test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import subprocess, tempfile
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'ir'))
    from byrd_ir import Seq, Alt, Lit, Pos, Rpos, Any, Arbno

    ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    pattern = Seq(Pos(0), Seq(
        Arbno(Alt(Alt(Lit("Bird"), Lit("Blue")), Any(ALPHA))),
        Rpos(0)
    ))

    src = compile_to_csharp(pattern, class_name="SnoMatch", subject="BlueGoldBirdFish")
    print(src)

    d = tempfile.mkdtemp()
    csfile = os.path.join(d, "SnoMatch.cs")
    with open(csfile, "w") as f: f.write(src)

    r = subprocess.run(["dotnet-script", csfile], capture_output=True, text=True)
    if r.returncode != 0:
        # Try csc / dotnet build
        proj = os.path.join(d, "SnoMatch.csproj")
        with open(proj, "w") as f:
            f.write('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>\n')
        r2 = subprocess.run(["dotnet", "build", d, "-o", d+"/out", "-v", "q"],
                            capture_output=True, text=True, cwd=d)
        if r2.returncode != 0:
            print("BUILD ERROR:", r2.stderr[-500:])
        else:
            out = subprocess.run([d+"/out/SnoMatch"], capture_output=True, text=True)
            print("Output:", repr(out.stdout.strip()))
            print("PASS" if "Success" in out.stdout else "FAIL")
    else:
        print("Output:", repr(r.stdout.strip()))
        print("PASS" if "Success" in r.stdout else "FAIL")
