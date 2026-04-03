// Snobol4Parser.cs — SNOBOL4 parser: .sno source → IrStmt[]
//
// Parses SNOBOL4 fixed-format source into IrStmt[] / IrNode trees.
// IrStmt mirrors STMT_t from scrip_cc.h.
// IrNode mirrors EXPR_t / EKind from ir.h.
//
// Line structure:
//   col 1 non-blank, non-* : label token, rest is body
//   col 1 space/tab        : no label, rest is body
//   col 1 = *              : comment — skip
//   col 1 = + or -         : continuation — append to previous body
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01b

namespace ScripInterp;

public static class Snobol4Parser
{
    // ── Public entry ─────────────────────────────────────────────────────────

    public static IrStmt[] ParseFile(string path) =>
        ParseSource(File.ReadAllText(path));

    public static IrStmt[] ParseSource(string source)
    {
        var logicalLines = SplitLogicalLines(source);
        var expanded = new List<(string? label, string body)>();
        foreach (var (lbl, body) in logicalLines)
        {
            var parts = SplitSemicolons(body);
            if (parts.Count == 0)
            {
                // Empty body — keep label-only entry so label table is populated
                expanded.Add((lbl, ""));
                continue;
            }
            for (int i = 0; i < parts.Count; i++)
                expanded.Add((i == 0 ? lbl : null, parts[i]));
        }
        return expanded.Select(ll => ParseBody(ll.label, ll.body)).ToArray();
    }

    private static List<string> SplitSemicolons(string s)
    {
        var parts = new List<string>();
        int depth = 0; bool inQ = false; char qc = '\0'; int start = 0;
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (inQ) { if (c == qc) inQ = false; continue; }
            if (c == '\'' || c == '"') { inQ = true; qc = c; continue; }
            if (c == '(' || c == '<') depth++;
            else if (c == ')' || c == '>') depth--;
            else if (c == ';' && depth == 0)
            {
                parts.Add(s[start..i].Trim());
                start = i + 1;
            }
        }
        parts.Add(s[start..].Trim());
        return parts.Where(p => p.Length > 0).ToList();
    }

    // ── Logical line splitter ─────────────────────────────────────────────────

    private static List<(string? label, string body)> SplitLogicalLines(string src)
    {
        var result     = new List<(string?, string)>();
        var rawLines   = src.Split('\n');
        string? curLabel = null;
        var curBody    = new System.Text.StringBuilder();
        bool hasCurrent = false;

        void Flush()
        {
            if (!hasCurrent) return;
            result.Add((curLabel, curBody.ToString().Trim()));
            curLabel   = null;
            curBody.Clear();
            hasCurrent = false;
        }

        foreach (var rawLine in rawLines)
        {
            var line = rawLine.TrimEnd('\r');
            if (line.Length == 0) continue;
            char col1 = line[0];
            if (col1 == '*') continue;
            if (col1 == '+' || col1 == '-')
            {
                if (hasCurrent) curBody.Append(line.Length > 1 ? line[1..] : "");
                continue;
            }
            Flush();
            hasCurrent = true;
            if (col1 != ' ' && col1 != '\t')
            {
                int end = 0;
                while (end < line.Length && line[end] != ' ' && line[end] != '\t') end++;
                curLabel = line[0..end].ToUpperInvariant();
                curBody.Append(end < line.Length ? line[end..] : "");
            }
            else
            {
                curLabel = null;
                curBody.Append(line);
            }
        }
        Flush();
        return result;
    }

    // ── Body parser ───────────────────────────────────────────────────────────

    private static IrStmt ParseBody(string? label, string body)
    {
        var t = body.Trim();

        if (string.Equals(t, "END", StringComparison.OrdinalIgnoreCase) ||
            t.StartsWith("END ", StringComparison.OrdinalIgnoreCase) ||
            t.StartsWith("END\t", StringComparison.OrdinalIgnoreCase))
            return new IrStmt { Label = label, IsEnd = true };

        if (string.IsNullOrEmpty(t))
            return new IrStmt { Label = label };

        var (bodyNoGoto, gU, gS, gF) = ExtractGotos(t);
        var (subjectStr, patternStr, replStr, hasEq) = SplitStatement(bodyNoGoto.Trim());

        SnoGoto? go = null;
        if (gU != null || gS != null || gF != null)
            go = new SnoGoto { OnSuccess = gS, OnFailure = gF, Uncond = gU };

        return new IrStmt
        {
            Label       = label,
            Subject     = string.IsNullOrEmpty(subjectStr)  ? null : ParseExpr(subjectStr),
            Pattern     = string.IsNullOrEmpty(patternStr)  ? null : ParseExpr(patternStr),
            HasEq       = hasEq,
            Replacement = (hasEq && !string.IsNullOrEmpty(replStr)) ? ParseExpr(replStr) : null,
            Go          = go,
        };
    }

    // ── Goto extraction ───────────────────────────────────────────────────────

    private static (string body, string? u, string? s, string? f) ExtractGotos(string line)
    {
        // SNOBOL4 goto: :(U)  |  :S(X)  |  :F(Y)  |  :S(X)F(Y)  |  :F(Y)S(X)
        // Scan right-to-left, skipping over quoted strings, to find (label) groups.
        string? gU = null, gS = null, gF = null;
        var work = line;
        bool progress = true;
        while (progress)
        {
            progress = false;
            var w = work.TrimEnd();
            if (!w.EndsWith(')')) break;

            // Find the matching '(' for the final ')' — scan right-to-left skipping quotes
            int close = w.Length - 1; // index of final ')'
            int open  = FindMatchingOpenParen(w, close);
            if (open <= 0) break;

            var inner  = w[(open + 1)..close].Trim().ToUpperInvariant();
            var before = w[..open].TrimEnd();

            if (before.EndsWith(":S", StringComparison.OrdinalIgnoreCase) && gS == null)
                { gS = inner; work = before[..^2].TrimEnd(); progress = true; }
            else if (before.EndsWith(":F", StringComparison.OrdinalIgnoreCase) && gF == null)
                { gF = inner; work = before[..^2].TrimEnd(); progress = true; }
            else if (before.EndsWith(':') && gU == null)
                { gU = inner; work = before[..^1].TrimEnd(); progress = true; }
            else if (before.EndsWith("S", StringComparison.OrdinalIgnoreCase) && gS == null)
            {
                var trimBefore = before.TrimEnd();
                if (trimBefore.Length > 0 && (trimBefore[^1] == 'S' || trimBefore[^1] == 's'))
                {
                    var candidate = trimBefore[..^1].TrimEnd();
                    if (candidate.EndsWith(')') || gF != null || gU != null)
                    { gS = inner; work = candidate; progress = true; }
                }
            }
            else if (before.EndsWith("F", StringComparison.OrdinalIgnoreCase) && gF == null)
            {
                var trimBefore = before.TrimEnd();
                if (trimBefore.Length > 0 && (trimBefore[^1] == 'F' || trimBefore[^1] == 'f'))
                {
                    var candidate = trimBefore[..^1].TrimEnd();
                    if (candidate.EndsWith(')') || gS != null || gU != null)
                    { gF = inner; work = candidate; progress = true; }
                }
            }
        }
        return (work, gU, gS, gF);
    }

    // Find the '(' that matches the ')' at closeIdx, scanning left respecting quoted strings.
    private static int FindMatchingOpenParen(string s, int closeIdx)
    {
        int depth = 0;
        var quoted = new bool[s.Length];
        bool q = false; char qch = '\0';
        for (int i = 0; i < s.Length; i++)
        {
            if (q) { quoted[i] = true; if (s[i] == qch) q = false; continue; }
            if (s[i] == '\'' || s[i] == '"') { q = true; qch = s[i]; }
        }
        // Scan right-to-left from closeIdx
        for (int i = closeIdx; i >= 0; i--)
        {
            if (quoted[i]) continue;
            if (s[i] == ')') depth++;
            else if (s[i] == '(') { depth--; if (depth == 0) return i; }
        }
        return -1;
    }

    // ── Statement body splitter ───────────────────────────────────────────────

    private static (string subject, string pattern, string repl, bool hasEq)
        SplitStatement(string body)
    {
        var tokens = TopLevelTokens(body);
        if (tokens.Count == 0) return ("", "", "", false);

        string subject = tokens[0];
        int eqIdx = -1;
        for (int i = 1; i < tokens.Count; i++)
        {
            if (tokens[i] == "=") { eqIdx = i; break; }
            if (tokens[i].StartsWith('=') && tokens[i].Length > 1)
                { eqIdx = i; tokens[i] = tokens[i][1..]; break; }
        }
        if (eqIdx >= 0)
            return (subject.Trim(),
                    string.Join(" ", tokens[1..eqIdx]).Trim(),
                    string.Join(" ", tokens[(eqIdx + 1)..]).Trim(),
                    true);
        return (subject.Trim(), string.Join(" ", tokens[1..]).Trim(), "", false);
    }

    private static List<string> TopLevelTokens(string s)
    {
        var tokens = new List<string>();
        var cur    = new System.Text.StringBuilder();
        int depth  = 0; bool inStr = false; char strCh = '\'';
        void Push() { if (cur.Length > 0) { tokens.Add(cur.ToString()); cur.Clear(); } }
        var chars = s.ToCharArray();
        int ci = 0;
        while (ci < chars.Length)
        {
            char c = chars[ci];
            if (inStr) { cur.Append(c); if (c == strCh) inStr = false; ci++; continue; }
            if (c == '\'' || c == '"') { inStr = true; strCh = c; cur.Append(c); ci++; continue; }
            if (c == '(' || c == '<') { depth++; cur.Append(c); ci++; continue; }
            if (c == ')' || c == '>') { depth--; cur.Append(c); ci++; continue; }
            if ((c == ' ' || c == '\t') && depth == 0) { Push(); ci++; continue; }
            // Treat ** as a single token
            if (c == '*' && ci + 1 < chars.Length && chars[ci + 1] == '*' && depth == 0)
            {
                Push(); cur.Append("**"); Push(); ci += 2; continue;
            }
            cur.Append(c); ci++;
        }
        Push();
        return tokens;
    }

    // ── Expression parser → IrNode ────────────────────────────────────────────

    public static IrNode ParseExpr(string src)
    {
        src = src.Trim();
        if (string.IsNullOrEmpty(src)) return IrNode.QStr("");

        // Alternation (|) — lowest precedence
        var altParts = SplitTopLevel(src, '|');
        if (altParts.Count > 1)
        {
            IrNode result = ParseConcat(altParts[^1].Trim());
            for (int i = altParts.Count - 2; i >= 0; i--)
                result = IrNode.Nary(IrKind.E_ALT, ParseConcat(altParts[i].Trim()), result);
            return result;
        }
        return ParseConcat(src);
    }

    // ── Infix expression parser ───────────────────────────────────────────────
    // SNOBOL4 precedence (low → high):
    //   + -   (additive)
    //   * /   (multiplicative)
    //   ^     (power, right-assoc)
    //   concat (juxtaposition, space-separated atoms with no operator between)
    //   unary -/+  (handled in ParseAtom)
    //
    // Token stream: TopLevelTokens splits on whitespace.
    // A bare +/-/*/^  token between two operands is an infix operator.
    // Adjacent non-operator tokens are concatenation.

    private static IrNode ParseConcat(string src)
    {
        var tokens = TopLevelTokens(src);
        if (tokens.Count == 0) return IrNode.QStr("");
        int pos = 0;
        return ParseAdditive(tokens, ref pos);
    }

    private static bool IsAddOp(string t, out IrKind kind)
    {
        if (t == "+") { kind = IrKind.E_ADD; return true; }
        if (t == "-") { kind = IrKind.E_SUB; return true; }
        kind = default; return false;
    }
    private static bool IsMulOp(string t, out IrKind kind)
    {
        if (t == "*") { kind = IrKind.E_MUL; return true; }
        if (t == "/") { kind = IrKind.E_DIV; return true; }
        kind = default; return false;
    }

    private static IrNode ParseAdditive(List<string> toks, ref int pos)
    {
        var left = ParseMultiplicative(toks, ref pos);
        while (pos < toks.Count && IsAddOp(toks[pos], out var kind))
        {
            pos++;
            var right = ParseMultiplicative(toks, ref pos);
            left = IrNode.Nary(kind, left, right);
        }
        return left;
    }

    private static IrNode ParseMultiplicative(List<string> toks, ref int pos)
    {
        var left = ParsePower(toks, ref pos);
        while (pos < toks.Count && IsMulOp(toks[pos], out var kind))
        {
            pos++;
            var right = ParsePower(toks, ref pos);
            left = IrNode.Nary(kind, left, right);
        }
        return left;
    }

    private static IrNode ParsePower(List<string> toks, ref int pos)
    {
        var left = ParseCatSequence(toks, ref pos);
        if (pos < toks.Count && (toks[pos] == "^" || toks[pos] == "**"))
        {
            pos++;
            var right = ParsePower(toks, ref pos); // right-associative
            return IrNode.Nary(IrKind.E_POW, left, right);
        }
        return left;
    }

    // Juxtaposition (space concat): adjacent non-operator atoms → E_CAT chain
    private static bool IsOperatorToken(string t) =>
        t == "+" || t == "-" || t == "*" || t == "/" || t == "^" || t == "**";

    // Capture operator tokens: ". V" "@V" "$ V" are binary in pattern context
    private static bool IsCaptureOp(string t, out IrKind kind)
    {
        if (t == ".")  { kind = IrKind.E_CAPT_COND_ASGN;  return true; }
        if (t == "@")  { kind = IrKind.E_CAPT_CURSOR;      return true; }
        if (t == "$")  { kind = IrKind.E_CAPT_IMMED_ASGN;  return true; }
        kind = default; return false;
    }

    private static IrNode ParseCatSequence(List<string> toks, ref int pos)
    {
        var parts = new List<IrNode>();
        while (pos < toks.Count && !IsOperatorToken(toks[pos]))
        {
            // Lone "." "@" "$" token followed by a variable — binary capture operator
            if (IsCaptureOp(toks[pos], out var capKind) &&
                pos + 1 < toks.Count && !IsOperatorToken(toks[pos + 1]))
            {
                var varName = toks[pos + 1].TrimStart('@', '.', '$').ToUpperInvariant();
                parts.Add(IrNode.Nary(capKind, IrNode.Var(varName)));
                pos += 2;
                continue;
            }
            parts.Add(ParseAtom(toks[pos]));
            pos++;
        }
        if (parts.Count == 0) return IrNode.QStr("");
        if (parts.Count == 1) return parts[0];
        // Right-associative concat
        IrNode result = parts[^1];
        for (int i = parts.Count - 2; i >= 0; i--)
            result = IrNode.Nary(IrKind.E_CAT, parts[i], result);
        return result;
    }

    private static IrNode ParseAtom(string src)
    {
        src = src.Trim();
        if (string.IsNullOrEmpty(src)) return IrNode.QStr("");

        // Quoted string
        if ((src.StartsWith('\'') && src.EndsWith('\'') && src.Length >= 2) ||
            (src.StartsWith('"')  && src.EndsWith('"')  && src.Length >= 2))
            return IrNode.QStr(src[1..^1]);

        // @var — cursor capture
        if (src.StartsWith('@') && src.Length > 1)
            return IrNode.Nary(IrKind.E_CAPT_CURSOR,
                               IrNode.Var(src[1..].ToUpperInvariant()));

        // .var — conditional capture
        if (src.StartsWith('.') && src.Length > 1 && !char.IsDigit(src[1]))
            return IrNode.Nary(IrKind.E_CAPT_COND_ASGN,
                               IrNode.Var(src[1..].ToUpperInvariant()));

        // $expr — indirect reference (or immediate capture in pattern context — handled by executor)
        if (src.StartsWith('$') && src.Length > 1)
            return IrNode.Nary(IrKind.E_INDIRECT, ParseAtom(src[1..]));

        // *expr — deferred pattern
        if (src.StartsWith('*') && src.Length > 1 && !char.IsDigit(src[1]))
            return IrNode.Nary(IrKind.E_DEFER, ParseAtom(src[1..]));

        // Unary minus / plus (only if followed by non-digit — otherwise it's a number)
        if (src == "-") return IrNode.QStr("-");
        if (src == "+") return IrNode.QStr("+");

        // Unary +/- with operand attached (e.g. +'4', -X, -(expr))
        if (src.StartsWith('+') && src.Length > 1)
            return IrNode.Nary(IrKind.E_PLS, ParseAtom(src[1..]));
        if (src.StartsWith('-') && src.Length > 1 && !char.IsDigit(src[1]))
            return IrNode.Nary(IrKind.E_MNS, ParseAtom(src[1..]));

        // Parenthesised group
        if (src.StartsWith('(') && src.EndsWith(')') && src.Length >= 2)
        {
            // Verify balanced — the outer parens are matched
            if (IsBalancedParen(src))
                return ParseExpr(src[1..^1]);
        }

        // Function call: NAME(args...)
        int pOpen = FindTopLevelParen(src);
        if (pOpen > 0 && src.EndsWith(')'))
        {
            var name  = src[..pOpen].Trim().ToUpperInvariant();
            var inner = src[(pOpen + 1)..^1];
            var argNodes = SplitTopLevel(inner, ',')
                           .Select(a => ParseExpr(a.Trim()))
                           .ToArray();
            // Arithmetic operators encoded as calls
            if (argNodes.Length == 2)
            {
                IrKind? bk = name switch
                {
                    "+" => IrKind.E_ADD, "-" => IrKind.E_SUB,
                    "*" => IrKind.E_MUL, "/" => IrKind.E_DIV,
                    "^" => IrKind.E_POW, "%" => IrKind.E_MOD,
                    _   => null
                };
                if (bk != null) return IrNode.Nary(bk.Value, argNodes[0], argNodes[1]);
            }
            if (argNodes.Length == 1 && name == "-")
                return IrNode.Nary(IrKind.E_MNS, argNodes[0]);
            if (argNodes.Length == 1 && name == "+")
                return IrNode.Nary(IrKind.E_PLS, argNodes[0]);

            // Map known SNOBOL4 pattern builtins to their IrKind
            IrKind? pk = name switch
            {
                "ARB"     => IrKind.E_ARB,
                "REM"     => IrKind.E_REM,
                "FAIL"    => IrKind.E_FAIL,
                "SUCCEED" => IrKind.E_SUCCEED,
                "FENCE"   => IrKind.E_FENCE,
                "ABORT"   => IrKind.E_ABORT,
                "BAL"     => IrKind.E_BAL,
                _         => null
            };
            if (pk != null && argNodes.Length == 0) return new IrNode(pk.Value);

            IrKind? pk1 = name switch
            {
                "ANY"    => IrKind.E_ANY,    "NOTANY" => IrKind.E_NOTANY,
                "SPAN"   => IrKind.E_SPAN,   "BREAK"  => IrKind.E_BREAK,
                "BREAKX" => IrKind.E_BREAKX, "LEN"    => IrKind.E_LEN,
                "TAB"    => IrKind.E_TAB,    "RTAB"   => IrKind.E_RTAB,
                "POS"    => IrKind.E_POS,    "RPOS"   => IrKind.E_RPOS,
                "ARBNO"  => IrKind.E_ARBNO,
                _        => null
            };
            if (pk1 != null && argNodes.Length == 1)
                return IrNode.Nary(pk1.Value, argNodes[0]);

            return new IrNode(IrKind.E_FNC) { SVal = name, Children = argNodes };
        }

        // Array/table subscript: name<idx,...>
        int aOpen = src.IndexOf('<');
        if (aOpen > 0 && src.EndsWith('>'))
        {
            var baseNode = ParseAtom(src[..aOpen]);
            var inner    = src[(aOpen + 1)..^1];
            var idxNodes = SplitTopLevel(inner, ',')
                           .Select(a => ParseExpr(a.Trim()))
                           .ToArray();
            return new IrNode(IrKind.E_IDX)
                   { Children = new[] { baseNode }.Concat(idxNodes).ToArray() };
        }

        // Keyword: &NAME
        if (src.StartsWith('&') && src.Length > 1)
            return IrNode.Keyword(src[1..].ToUpperInvariant());

        // Nullary pattern builtins (no parens)
        IrKind? nk = src.ToUpperInvariant() switch
        {
            "ARB"     => IrKind.E_ARB,     "REM"     => IrKind.E_REM,
            "FAIL"    => IrKind.E_FAIL,    "SUCCEED" => IrKind.E_SUCCEED,
            "FENCE"   => IrKind.E_FENCE,   "ABORT"   => IrKind.E_ABORT,
            "BAL"     => IrKind.E_BAL,
            _         => null
        };
        if (nk != null) return new IrNode(nk.Value);

        // Integer literal
        if (long.TryParse(src, out var ival))
            return IrNode.Int(ival);

        // Real literal
        if (double.TryParse(src,
                System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture,
                out var dval))
            return IrNode.Float(dval);

        // Variable reference
        return IrNode.Var(src.ToUpperInvariant());
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static List<string> SplitTopLevel(string s, char sep)
    {
        var parts = new List<string>();
        var cur   = new System.Text.StringBuilder();
        int depth = 0; bool inStr = false; char strCh = '\'';
        foreach (char c in s)
        {
            if (inStr) { cur.Append(c); if (c == strCh) inStr = false; continue; }
            if (c == '\'' || c == '"') { inStr = true; strCh = c; cur.Append(c); continue; }
            if (c == '(' || c == '<') { depth++; cur.Append(c); continue; }
            if (c == ')' || c == '>') { depth--; cur.Append(c); continue; }
            if (c == sep && depth == 0) { parts.Add(cur.ToString()); cur.Clear(); continue; }
            cur.Append(c);
        }
        parts.Add(cur.ToString());
        return parts;
    }

    // Find the opening paren of a top-level function call: "NAME(" — return index of '('
    private static int FindTopLevelParen(string s)
    {
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (c == '(') return i;
            if (c == ' ' || c == '\t') return -1;
        }
        return -1;
    }

    private static bool IsBalancedParen(string s)
    {
        if (!s.StartsWith('(') || !s.EndsWith(')')) return false;
        int depth = 0; bool inStr = false; char strCh = '\'';
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (inStr) { if (c == strCh) inStr = false; continue; }
            if (c == '\'' || c == '"') { inStr = true; strCh = c; continue; }
            if (c == '(') depth++;
            else if (c == ')') { depth--; if (depth == 0 && i < s.Length - 1) return false; }
        }
        return depth == 0;
    }
}
