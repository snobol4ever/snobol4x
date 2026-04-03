// Executor.cs — 5-phase SNOBOL4 statement executor
//
// Mirrors execute_program() + interp_eval() in src/driver/scrip-interp.c
// and stmt_exec_dyn() in src/runtime/dyn/stmt_exec.c
//
// Phase 1: resolve subject → string value (Σ)
// Phase 2: build IByrdBox graph from pattern node
// Phase 3: ByrdBoxExecutor.Run() — scan loop (cursor = Δ)
// Phase 4: evaluate replacement expression
// Phase 5: splice replacement into subject, commit captures, branch :S/:F
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01

using Snobol4.Runtime.Boxes;

namespace ScripInterp;

public sealed class Executor
{
    private readonly SnobolEnv _env;
    private readonly TextWriter _output;
    private readonly TextReader _input;

    // Call stack for user-defined functions
    private sealed class CallFrame
    {
        public string   FuncName   { get; init; } = "";
        public string[] SavedNames { get; init; } = Array.Empty<string>();
        public SnobolVal[] SavedVals { get; init; } = Array.Empty<SnobolVal>();
    }
    private readonly Stack<CallFrame> _callStack = new();

    // Exception types for RETURN/FRETURN/NRETURN control flow
    private sealed class ReturnException   : Exception { public SnobolVal Value; public ReturnException(SnobolVal v) : base() { Value = v; } }
    private sealed class FReturnException  : Exception { public FReturnException() : base() { } }
    private sealed class NReturnException  : Exception { public string VarName; public NReturnException(string n) : base() { VarName = n; } }

    public Executor(SnobolEnv env, TextWriter? output = null, TextReader? input = null)
    {
        _env    = env;
        _output = output ?? Console.Out;
        _input  = input  ?? Console.In;
    }

    // ── Entry point ──────────────────────────────────────────────────────────

    public void Run(Stmt[] program)
    {
        // Build label table
        var labels = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < program.Length; i++)
            if (program[i].Label != null)
                labels[program[i].Label!] = i;

        // Pre-scan DEFINE statements
        PrescanDefines(program);

        int pc = 0;
        int stepLimit = 5_000_000;

        while (pc < program.Length && stepLimit-- > 0)
        {
            var stmt = program[pc];

            if (stmt.IsEnd) break;

            // Handle OUTPUT side-effect for assignment: OUTPUT = expr
            if (stmt.Subject is Var("OUTPUT") && stmt.HasEq && stmt.Pattern == null)
            {
                var val = EvalNode(stmt.Replacement ?? new SLit(""));
                if (!val.IsFail)
                    _output.WriteLine(val.ToString());
                pc++;
                continue;
            }

            bool succeeded = ExecStmt(stmt, labels, program, out string? gotoTarget);

            // Goto resolution
            string? target = gotoTarget;
            if (target == null && stmt.GotoU != null) target = stmt.GotoU;
            else if (succeeded  && stmt.GotoS != null) target = stmt.GotoS;
            else if (!succeeded && stmt.GotoF != null) target = stmt.GotoF;

            if (target != null)
            {
                if (string.Equals(target, "END", StringComparison.OrdinalIgnoreCase)) break;
                if (labels.TryGetValue(target, out var dest)) { pc = dest; continue; }
                // Unknown label → treat as fall-through (or could error)
            }

            pc++;
        }
    }

    // ── Single statement execution ────────────────────────────────────────────

    private bool ExecStmt(Stmt stmt, Dictionary<string,int> labels, Stmt[] prog, out string? gotoOverride)
    {
        gotoOverride = null;

        // Pure goto: no subject, no pattern, no eq
        if (stmt.Subject == null && !stmt.HasEq && stmt.Pattern == null)
            return true;

        // ── Phase 1: resolve subject ─────────────────────────────────────────
        string? subjName  = null;
        SnobolVal subjVal = SnobolVal.Null;

        if (stmt.Subject != null)
        {
            if (stmt.Subject is Var(var sname))
            {
                subjName = sname;
                subjVal  = _env.Get(sname);
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

            // Special: OUTPUT = ...
            if (subjName == "OUTPUT")
            {
                _output.WriteLine(replVal.ToString());
                return true;
            }
            // &ANCHOR etc.
            if (stmt.Subject is Var(var asn))
            {
                _env.Set(asn, replVal);
                return true;
            }
            // Indirect: $var = ...
            if (stmt.Subject is IndirectRef(var inner))
            {
                var name = EvalNode(inner).ToString();
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
                getPatternVar: n      => null,   // pattern vars: future
                evalNode:      EvalNode
            );

            IByrdBox root;
            try { root = builder.Build(stmt.Pattern); }
            catch { return false; }

            // Phase 3: run scan loop
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
                var before  = subject[..result.MatchStart];
                var after   = subject[(result.MatchStart + result.MatchLength)..];
                var newSubj = before + replVal!.ToString() + after;
                _env.Set(subjName, SnobolVal.Of(newSubj));
            }

            return true;
        }

        // Subject-only (evaluation for side effects / fail check)
        if (subjVal.IsFail) return false;
        return true;
    }

    // ── Expression evaluator ─────────────────────────────────────────────────

    public SnobolVal EvalNode(Node? n)
    {
        if (n == null) return SnobolVal.Null;

        return n switch
        {
            SLit(var v) => SnobolVal.Of(v),
            NLit(var v) => long.TryParse(v, out var iv) ? SnobolVal.Of(iv)
                         : double.TryParse(v, System.Globalization.NumberStyles.Float,
                               System.Globalization.CultureInfo.InvariantCulture, out var dv)
                           ? SnobolVal.Of(dv) : SnobolVal.Of(v),

            Var(var name) => EvalVar(name),

            // Concatenation: evaluate both sides, convert to string, concatenate
            Cat(var l, var r) => SnobolVal.Of(EvalNode(l).ToString() + EvalNode(r).ToString()),

            // Indirect reference: $expr → eval expr → use as variable name
            IndirectRef(var inner) => _env.Get(EvalNode(inner).ToString()),

            // Arithmetic unary/binary — map through FncCall nodes from parser
            // Function calls
            FncCall(var name, var args) => EvalFnc(name, args),

            // Array subscript
            ArrayRef(var baseN, var idxN) => EvalArrayRef(baseN, idxN),

            // Captures in expr context — evaluate inner only
            CaptCond(var inner, _) => EvalNode(inner),
            CaptImm(var inner, _)  => EvalNode(inner),
            CaptCursor(_)          => SnobolVal.Null,
            DeferredPat(var inner) => EvalNode(inner),

            Alt(var l, var r) =>  // alternation in expr → concat (shouldn't appear)
                SnobolVal.Of(EvalNode(l).ToString() + EvalNode(r).ToString()),

            Seq(var l, var r) => SnobolVal.Of(EvalNode(l).ToString() + EvalNode(r).ToString()),

            _ => SnobolVal.Null
        };
    }

    private SnobolVal EvalVar(string name)
    {
        // System keywords: &ANCHOR etc. stored with & prefix
        if (name.StartsWith('&'))
            return _env.Get(name);

        // INPUT — read a line
        if (string.Equals(name, "INPUT", StringComparison.OrdinalIgnoreCase))
        {
            var line = _input.ReadLine();
            return line == null ? SnobolVal.Fail : SnobolVal.Of(line);
        }

        return _env.Get(name);
    }

    private SnobolVal EvalFnc(string name, Node[] args)
    {
        var uname = name.ToUpperInvariant();

        // Arithmetic operators encoded as function calls by the parser
        if (uname == "+" && args.Length == 2)
            return Arith(EvalNode(args[0]), EvalNode(args[1]), '+');
        if (uname == "-" && args.Length == 2)
            return Arith(EvalNode(args[0]), EvalNode(args[1]), '-');
        if (uname == "*" && args.Length == 2)
            return Arith(EvalNode(args[0]), EvalNode(args[1]), '*');
        if (uname == "/" && args.Length == 2)
            return Arith(EvalNode(args[0]), EvalNode(args[1]), '/');
        if (uname == "^" && args.Length == 2)
            return Arith(EvalNode(args[0]), EvalNode(args[1]), '^');
        if (uname == "-" && args.Length == 1) // unary minus
        {
            var v = EvalNode(args[0]);
            return v.Type == DType.Real ? SnobolVal.Of(-v.Real) : SnobolVal.Of(-v.ToInt());
        }

        // DEFINE — register function at runtime
        if (uname == "DEFINE")
        {
            var spec  = args.Length >= 1 ? EvalNode(args[0]).ToString() : "";
            var entry = args.Length >= 2 ? EvalNode(args[1]).ToString() : null;
            if (!string.IsNullOrEmpty(spec))
                _env.DefineFunc(spec, string.IsNullOrEmpty(entry) ? null : entry);
            return SnobolVal.Null;
        }

        // OUTPUT(x) as function call
        if (uname == "OUTPUT")
        {
            if (args.Length >= 1) _output.WriteLine(EvalNode(args[0]).ToString());
            return SnobolVal.Null;
        }

        // User-defined function?
        var funcDef = _env.GetFunc(uname);
        if (funcDef != null)
        {
            var evaledArgs = args.Select(EvalNode).ToArray();
            return CallUserFunc(uname, funcDef, evaledArgs);
        }

        // Builtin
        var evalArgs = args.Select(EvalNode).ToArray();
        return _env.CallBuiltin(uname, evalArgs);
    }

    private SnobolVal EvalArrayRef(Node baseN, Node[] idxN)
    {
        // Stub: arrays not yet implemented — return Null
        return SnobolVal.Null;
    }

    private static SnobolVal Arith(SnobolVal a, SnobolVal b, char op)
    {
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
                _   => SnobolVal.Fail
            };
        }
    }

    // ── User function call ────────────────────────────────────────────────────

    private SnobolVal CallUserFunc(string name, SnobolEnv.FuncDef def, SnobolVal[] args)
    {
        if (_callStack.Count > 256) return SnobolVal.Fail;

        // Save + bind params and locals
        var allNames = new[] { name }.Concat(def.Params).Concat(def.Locals).ToArray();
        var saved    = allNames.Select(n => _env.Get(n)).ToArray();

        _env.Set(name, SnobolVal.Null);  // clear return slot
        for (int i = 0; i < def.Params.Length; i++)
            _env.Set(def.Params[i], i < args.Length ? args[i] : SnobolVal.Null);
        foreach (var loc in def.Locals)
            _env.Set(loc, SnobolVal.Null);

        _callStack.Push(new CallFrame { FuncName = name, SavedNames = allNames, SavedVals = saved });

        // We need our own mini program-counter loop over the body
        // For now: the body is in the top-level program array — we need a reference.
        // This is resolved in RunWithLabels; we store labels globally via RunBody.
        // This call path is only reachable when RunWithLabels has set _labels/_program.
        SnobolVal retval = SnobolVal.Null;
        try
        {
            retval = RunBody(def.BodyLabel);
        }
        catch (ReturnException rx)  { retval = rx.Value; }
        catch (FReturnException)    { retval = SnobolVal.Fail; }
        catch (NReturnException nx)
        {
            // Return name-ref: caller can assign through it
            retval = SnobolVal.Of(nx.VarName);
        }
        finally
        {
            // Restore saved variables
            var frame = _callStack.Pop();
            for (int i = 0; i < frame.SavedNames.Length; i++)
                _env.Set(frame.SavedNames[i], frame.SavedVals[i]);
        }

        return retval;
    }

    // ── Program context (set during Run for use by CallUserFunc) ─────────────

    private Stmt[]? _program;
    private Dictionary<string,int>? _labels;

    private SnobolVal RunBody(string entryLabel)
    {
        if (_program == null || _labels == null) return SnobolVal.Null;
        if (!_labels.TryGetValue(entryLabel, out int pc)) return SnobolVal.Null;

        int limit = 1_000_000;
        while (pc < _program.Length && limit-- > 0)
        {
            var stmt = _program[pc];
            if (stmt.IsEnd) break;

            bool ok = ExecStmt(stmt, _labels, _program, out string? gt);

            string? target = gt;
            if (target == null && stmt.GotoU != null) target = stmt.GotoU;
            else if (ok  && stmt.GotoS != null) target = stmt.GotoS;
            else if (!ok && stmt.GotoF != null) target = stmt.GotoF;

            if (target != null)
            {
                var t = target.ToUpperInvariant();
                if (t == "RETURN")  throw new ReturnException(_env.Get(_callStack.Peek().FuncName));
                if (t == "FRETURN") throw new FReturnException();
                if (t == "NRETURN") { var rv = _env.Get(_callStack.Peek().FuncName); throw new NReturnException(rv.ToString()); }
                if (t == "END") break;
                if (_labels.TryGetValue(t, out var dest)) { pc = dest; continue; }
            }
            pc++;
        }

        return _env.Get(_callStack.Count > 0 ? _callStack.Peek().FuncName : "");
    }

    private void PrescanDefines(Stmt[] program)
    {
        // Store program/labels for use by CallUserFunc
        _program = program;
        _labels  = new Dictionary<string,int>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < program.Length; i++)
            if (program[i].Label != null)
                _labels[program[i].Label!] = i;

        foreach (var stmt in program)
        {
            if (stmt.Subject is FncCall("DEFINE", var dargs) && dargs.Length >= 1)
            {
                var spec  = EvalNode(dargs[0]).ToString();
                var entry = dargs.Length >= 2 ? EvalNode(dargs[1]).ToString() : null;
                if (!string.IsNullOrEmpty(spec))
                    _env.DefineFunc(spec, string.IsNullOrEmpty(entry) ? null : entry);
            }
        }

        // Re-initialise labels (prescan may have mutated nothing, just ensure _labels set)
    }
}
