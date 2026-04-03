// Executor.cs — 5-phase SNOBOL4 statement executor
//
// Dispatches on IrKind (not bespoke Node records).
// Mirrors execute_program() + interp_eval() in scrip-interp.c
// and stmt_exec_dyn() in src/runtime/dyn/stmt_exec.c
//
// Phase 1: resolve subject → SnobolVal (Σ)
// Phase 2: PatternBuilder walks IrNode pattern subtree → IByrdBox graph
// Phase 3: ByrdBoxExecutor.Run() — scan loop (Δ cursor)
// Phase 4: evaluate replacement IrNode subtree → SnobolVal
// Phase 5: splice into subject, commit captures, :S/:F branch
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01b

using Snobol4.Runtime.Boxes;

namespace ScripInterp;

public sealed class Executor
{
    private readonly SnobolEnv   _env;
    private readonly TextWriter  _output;
    private readonly TextReader  _input;

    // Program context — set during Run(), used by user function calls
    private IrStmt[]?               _program;
    private Dictionary<string,int>? _labels;

    // Exception types for RETURN / FRETURN / NRETURN
    private sealed class ReturnException  : Exception { public SnobolVal Value; public ReturnException(SnobolVal v) : base() { Value = v; } }
    private sealed class FReturnException : Exception { public FReturnException() : base() { } }
    private sealed class NReturnException : Exception { public string VarName; public NReturnException(string n) : base() { VarName = n; } }

    // Call stack
    private sealed class CallFrame
    {
        public required string     FuncName   { get; init; }
        public required string[]   SavedNames { get; init; }
        public required SnobolVal[] SavedVals  { get; init; }
    }
    private readonly Stack<CallFrame> _callStack = new();

    public Executor(SnobolEnv env, TextWriter? output = null, TextReader? input = null)
    {
        _env    = env;
        _output = output ?? Console.Out;
        _input  = input  ?? Console.In;
    }

    // ── Entry point ──────────────────────────────────────────────────────────

    public void Run(IrStmt[] program)
    {
        _program = program;
        _labels  = BuildLabelTable(program);
        PrescanDefines(program);

        int pc        = 0;
        int stepLimit = 5_000_000;

        while (pc < program.Length && stepLimit-- > 0)
        {
            var stmt = program[pc];
            if (stmt.IsEnd) break;

            bool succeeded = ExecStmt(stmt, out string? gotoOverride);

            string? target = gotoOverride
                ?? stmt.Go?.Uncond
                ?? (succeeded  ? stmt.Go?.OnSuccess : stmt.Go?.OnFailure);

            if (target != null)
            {
                if (IsSpecialLabel(target)) break;
                if (_labels.TryGetValue(target, out var dest)) { pc = dest; continue; }
            }
            pc++;
        }
    }

    // ── Single statement ─────────────────────────────────────────────────────

    private bool ExecStmt(IrStmt stmt, out string? gotoOverride)
    {
        gotoOverride = null;

        // Empty / pure-goto statement
        if (stmt.Subject == null && !stmt.HasEq && stmt.Pattern == null)
            return true;

        // ── Phase 1: resolve subject ─────────────────────────────────────────
        string?   subjName = null;
        SnobolVal subjVal  = SnobolVal.Null;

        if (stmt.Subject != null)
        {
            if (stmt.Subject.Kind == IrKind.E_VAR)
            {
                subjName = stmt.Subject.SVal!;
                subjVal  = _env.Get(subjName);
            }
            else
            {
                subjVal = EvalNode(stmt.Subject);
            }
        }

        // ── Assignment only (no pattern) ─────────────────────────────────────
        if (stmt.Pattern == null && stmt.HasEq)
        {
            var replVal = stmt.Replacement != null ? EvalNode(stmt.Replacement) : SnobolVal.Null;
            if (replVal.IsFail) return false;

            if (subjName == "OUTPUT")
            { _output.WriteLine(replVal.ToString()); return true; }

            if (stmt.Subject?.Kind == IrKind.E_VAR)
            { _env.Set(subjName!, replVal); return true; }

            if (stmt.Subject?.Kind == IrKind.E_KEYWORD)
            { _env.Set("&" + stmt.Subject.SVal!, replVal); return true; }

            if (stmt.Subject?.Kind == IrKind.E_INDIRECT)
            {
                var name = EvalNode(stmt.Subject.Children[0]).ToString();
                if (string.IsNullOrEmpty(name)) return false;
                _env.Set(name, replVal);
                return true;
            }
            return false;
        }

        // ── Pattern statement ────────────────────────────────────────────────
        if (stmt.Pattern != null)
        {
            // Phase 2: build box graph
            var builder = new PatternBuilder(
                setVar:        (n, v) => _env.Set(n, SnobolVal.Of(v)),
                getStringVar:  n      => _env.Get(n).ToString(),
                getPatternVar: n      => null,
                evalNode:      EvalNode
            );

            IByrdBox root;
            try   { root = builder.Build(stmt.Pattern); }
            catch { return false; }

            // Phase 3: scan loop
            var subject = subjVal.ToString();
            var anchor  = _env.Get("&ANCHOR").ToInt() != 0;
            var ms      = new MatchState(subject);
            var exec    = new ByrdBoxExecutor(root, builder.Captures);
            var result  = exec.Run(ms, anchor);

            if (!result.Success) return false;

            // Phase 4: evaluate replacement
            SnobolVal? replVal = null;
            if (stmt.HasEq && stmt.Replacement != null)
            {
                var rv = EvalNode(stmt.Replacement);
                if (rv.IsFail) return false;
                replVal = rv;
            }

            // Phase 5: splice + commit captures + write subject
            if (replVal != null && subjName != null)
            {
                var newSubj = subject[..result.MatchStart]
                            + replVal.ToString()
                            + subject[(result.MatchStart + result.MatchLength)..];
                _env.Set(subjName, SnobolVal.Of(newSubj));
            }

            return true;
        }

        // Subject-only (side-effect / fail check)
        if (subjVal.IsFail) return false;
        return true;
    }

    // ── Expression evaluator ─────────────────────────────────────────────────

    public SnobolVal EvalNode(IrNode? n)
    {
        if (n == null) return SnobolVal.Null;

        return n.Kind switch
        {
            IrKind.E_QLIT    => SnobolVal.Of(n.SVal ?? ""),
            IrKind.E_ILIT    => SnobolVal.Of(n.IVal),
            IrKind.E_FLIT    => SnobolVal.Of(n.DVal),
            IrKind.E_NUL     => SnobolVal.Null,

            IrKind.E_VAR     => EvalVar(n.SVal!),
            IrKind.E_KEYWORD => _env.Get("&" + n.SVal!),

            IrKind.E_INDIRECT =>
                _env.Get(EvalNode(n.Children[0]).ToString()),

            IrKind.E_CAT     => EvalCat(n),
            IrKind.E_SEQ     => EvalCat(n),   // in value context, SEQ = CAT

            IrKind.E_ADD     => Arith(n, '+'),
            IrKind.E_SUB     => Arith(n, '-'),
            IrKind.E_MUL     => Arith(n, '*'),
            IrKind.E_DIV     => Arith(n, '/'),
            IrKind.E_POW     => Arith(n, '^'),
            IrKind.E_MOD     => Arith(n, '%'),
            IrKind.E_MNS     => Negate(n),
            IrKind.E_PLS     => EvalNode(n.Children[0]),

            IrKind.E_FNC     => EvalFnc(n),
            IrKind.E_IDX     => EvalIdx(n),
            IrKind.E_ASSIGN  => EvalAssign(n),

            // Captures in value context — evaluate inner expression only
            IrKind.E_CAPT_COND_ASGN  => n.Children.Length > 0 ? EvalNode(n.Children[0]) : SnobolVal.Null,
            IrKind.E_CAPT_IMMED_ASGN => n.Children.Length > 0 ? EvalNode(n.Children[0]) : SnobolVal.Null,
            IrKind.E_CAPT_CURSOR     => SnobolVal.Null,
            IrKind.E_DEFER           => n.Children.Length > 0 ? EvalNode(n.Children[0]) : SnobolVal.Null,

            // ALT in value context — evaluate left (shouldn't normally appear)
            IrKind.E_ALT     => n.Children.Length > 0 ? EvalNode(n.Children[0]) : SnobolVal.Null,

            IrKind.E_NAME    => n.Children.Length > 0 ? EvalNode(n.Children[0]) : SnobolVal.Null,

            _ => SnobolVal.Null
        };
    }

    private SnobolVal EvalVar(string name)
    {
        if (name == "OUTPUT") return SnobolVal.Null;
        if (name == "INPUT")
        {
            var line = _input.ReadLine();
            return line == null ? SnobolVal.Fail : SnobolVal.Of(line);
        }
        return _env.Get(name);
    }

    private SnobolVal EvalCat(IrNode n)
    {
        // n-ary: right-recursive tree, flatten and concat
        var sb = new System.Text.StringBuilder();
        void Collect(IrNode node)
        {
            if (node.Kind == IrKind.E_CAT || node.Kind == IrKind.E_SEQ)
            {
                foreach (var c in node.Children) Collect(c);
            }
            else sb.Append(EvalNode(node).ToString());
        }
        Collect(n);
        return SnobolVal.Of(sb.ToString());
    }

    private SnobolVal Arith(IrNode n, char op)
    {
        if (n.Children.Length < 2) return SnobolVal.Fail;
        var a = EvalNode(n.Children[0]);
        var b = EvalNode(n.Children[1]);
        if (a.IsFail || b.IsFail) return SnobolVal.Fail;
        bool useReal = a.Type == DType.Real || b.Type == DType.Real;
        if (useReal)
        {
            double av = a.Type == DType.Real ? a.Real : a.ToInt();
            double bv = b.Type == DType.Real ? b.Real : b.ToInt();
            return op switch
            {
                '+' => SnobolVal.Of(av + bv),
                '-' => SnobolVal.Of(av - bv),
                '*' => SnobolVal.Of(av * bv),
                '/' => bv == 0 ? SnobolVal.Fail : SnobolVal.Of(av / bv),
                '^' => SnobolVal.Of(Math.Pow(av, bv)),
                '%' => bv == 0 ? SnobolVal.Fail : SnobolVal.Of(av % bv),
                _   => SnobolVal.Fail
            };
        }
        else
        {
            long av = a.ToInt(), bv = b.ToInt();
            return op switch
            {
                '+' => SnobolVal.Of(av + bv),
                '-' => SnobolVal.Of(av - bv),
                '*' => SnobolVal.Of(av * bv),
                '/' => bv == 0 ? SnobolVal.Fail : SnobolVal.Of(av / bv),
                '^' => SnobolVal.Of((long)Math.Pow(av, bv)),
                '%' => bv == 0 ? SnobolVal.Fail : SnobolVal.Of(av % bv),
                _   => SnobolVal.Fail
            };
        }
    }

    private SnobolVal Negate(IrNode n)
    {
        if (n.Children.Length == 0) return SnobolVal.Fail;
        var v = EvalNode(n.Children[0]);
        if (v.IsFail) return SnobolVal.Fail;
        return v.Type == DType.Real ? SnobolVal.Of(-v.Real) : SnobolVal.Of(-v.ToInt());
    }

    private SnobolVal EvalFnc(IrNode n)
    {
        var name = n.SVal?.ToUpperInvariant() ?? "";
        var args = n.Children;

        if (name == "DEFINE")
        {
            var spec  = args.Length >= 1 ? EvalNode(args[0]).ToString() : "";
            var entry = args.Length >= 2 ? EvalNode(args[1]).ToString() : null;
            if (!string.IsNullOrEmpty(spec))
                _env.DefineFunc(spec, string.IsNullOrEmpty(entry) ? null : entry);
            return SnobolVal.Null;
        }

        if (name == "OUTPUT")
        {
            if (args.Length >= 1) _output.WriteLine(EvalNode(args[0]).ToString());
            return SnobolVal.Null;
        }

        var funcDef = _env.GetFunc(name);
        if (funcDef != null)
        {
            var evaledArgs = args.Select(EvalNode).ToArray();
            return CallUserFunc(name, funcDef, evaledArgs);
        }

        var evalArgs = args.Select(EvalNode).ToArray();
        return _env.CallBuiltin(name, evalArgs);
    }

    private SnobolVal EvalIdx(IrNode n)
    {
        // Children[0] = base, [1..] = indices
        if (n.Children.Length == 0) return SnobolVal.Null;
        var baseVal = EvalNode(n.Children[0]);
        // Array/table subscript — stub: needs SnobolVal.Array support
        return SnobolVal.Null;
    }

    private SnobolVal EvalAssign(IrNode n)
    {
        if (n.Children.Length < 2) return SnobolVal.Null;
        var val = EvalNode(n.Children[1]);
        var target = n.Children[0];
        if (target.Kind == IrKind.E_VAR) _env.Set(target.SVal!, val);
        return val;
    }

    // ── User function call ────────────────────────────────────────────────────

    private SnobolVal CallUserFunc(string name, SnobolEnv.FuncDef def, SnobolVal[] args)
    {
        if (_callStack.Count > 256) return SnobolVal.Fail;

        var allNames = new[] { name }.Concat(def.Params).Concat(def.Locals).ToArray();
        var saved    = allNames.Select(n => _env.Get(n)).ToArray();

        _env.Set(name, SnobolVal.Null);
        for (int i = 0; i < def.Params.Length; i++)
            _env.Set(def.Params[i], i < args.Length ? args[i] : SnobolVal.Null);
        foreach (var loc in def.Locals)
            _env.Set(loc, SnobolVal.Null);

        _callStack.Push(new CallFrame { FuncName = name, SavedNames = allNames, SavedVals = saved });

        SnobolVal retval = SnobolVal.Null;
        try   { retval = RunBody(def.BodyLabel); }
        catch (ReturnException rx)  { retval = rx.Value; }
        catch (FReturnException)    { retval = SnobolVal.Fail; }
        catch (NReturnException nx) { retval = SnobolVal.Of(nx.VarName); }
        finally
        {
            var frame = _callStack.Pop();
            for (int i = 0; i < frame.SavedNames.Length; i++)
                _env.Set(frame.SavedNames[i], frame.SavedVals[i]);
        }
        return retval;
    }

    private SnobolVal RunBody(string entryLabel)
    {
        if (_program == null || _labels == null) return SnobolVal.Null;
        if (!_labels.TryGetValue(entryLabel, out int pc)) return SnobolVal.Null;

        int limit = 1_000_000;
        while (pc < _program.Length && limit-- > 0)
        {
            var stmt = _program[pc];
            if (stmt.IsEnd) break;

            bool ok = ExecStmt(stmt, out string? gt);
            string? target = gt
                ?? stmt.Go?.Uncond
                ?? (ok  ? stmt.Go?.OnSuccess : stmt.Go?.OnFailure);

            if (target != null)
            {
                var tu = target.ToUpperInvariant();
                if (tu == "RETURN")
                {
                    var rv = _env.Get(_callStack.Peek().FuncName);
                    throw new ReturnException(rv);
                }
                if (tu == "FRETURN") throw new FReturnException();
                if (tu == "NRETURN")
                {
                    var rv = _env.Get(_callStack.Peek().FuncName);
                    throw new NReturnException(rv.ToString());
                }
                if (IsSpecialLabel(tu)) break;
                if (_labels.TryGetValue(tu, out var dest)) { pc = dest; continue; }
            }
            pc++;
        }
        return _env.Get(_callStack.Count > 0 ? _callStack.Peek().FuncName : "");
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static Dictionary<string,int> BuildLabelTable(IrStmt[] program)
    {
        var d = new Dictionary<string,int>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < program.Length; i++)
            if (program[i].Label != null)
                d[program[i].Label!] = i;
        return d;
    }

    private void PrescanDefines(IrStmt[] program)
    {
        foreach (var stmt in program)
        {
            if (stmt.Subject?.Kind == IrKind.E_FNC &&
                string.Equals(stmt.Subject.SVal, "DEFINE", StringComparison.OrdinalIgnoreCase))
            {
                var args  = stmt.Subject.Children;
                var spec  = args.Length >= 1 ? EvalNode(args[0]).ToString() : "";
                var entry = args.Length >= 2 ? EvalNode(args[1]).ToString() : null;
                if (!string.IsNullOrEmpty(spec))
                    _env.DefineFunc(spec, string.IsNullOrEmpty(entry) ? null : entry);
            }
        }
    }

    private static bool IsSpecialLabel(string t) =>
        string.Equals(t, "END",     StringComparison.OrdinalIgnoreCase) ||
        string.Equals(t, "RETURN",  StringComparison.OrdinalIgnoreCase) ||
        string.Equals(t, "FRETURN", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(t, "NRETURN", StringComparison.OrdinalIgnoreCase);
}
