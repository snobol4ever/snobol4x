// Snobol4Parser.cs — Pidgin parser for SNOBOL4 source files
//
// Parses .sno source into Stmt[] AST.
// Handles the SNOBOL4 fixed-format line structure:
//   col 1     : label (non-blank, non-*)
//   col 2+    : body   (subject [pattern] [= replacement] [goto])
//   col 1 = * : comment
//
// Continuation lines: col 1 = '+' or '-' (append to previous logical line).
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01

using Pidgin;
using Pidgin.Expression;
using static Pidgin.Parser;
using static Pidgin.Parser<char>;

namespace ScripInterp;

public static class Snobol4Parser
{
    // ── Public entry ─────────────────────────────────────────────────────────

    public static Stmt[] ParseFile(string path) =>
        ParseSource(File.ReadAllText(path));

    public static Stmt[] ParseSource(string source)
    {
        var logicalLines = SplitLogicalLines(source);
        var stmts = new List<Stmt>();
        foreach (var (label, body) in logicalLines)
        {
            var stmt = ParseBody(label, body);
            stmts.Add(stmt);
        }
        return stmts.ToArray();
    }

    // ── Logical line splitter ─────────────────────────────────────────────────
    // Handles continuation (+/-), comments (*), and END.

    private static List<(string? label, string body)> SplitLogicalLines(string src)
    {
        var result   = new List<(string?, string)>();
        var rawLines = src.Split('\n');
        string? curLabel = null;
        var     curBody  = new System.Text.StringBuilder();
        bool    hasCurrent = false;

        void Flush()
        {
            if (!hasCurrent) return;
            result.Add((curLabel, curBody.ToString().Trim()));
            curLabel  = null;
            curBody.Clear();
            hasCurrent = false;
        }

        foreach (var rawLine in rawLines)
        {
            // Normalise CRLF
            var line = rawLine.TrimEnd('\r');
            if (line.Length == 0) continue;

            char col1 = line[0];

            // Comment
            if (col1 == '*') continue;

            // Continuation
            if (col1 == '+' || col1 == '-')
            {
                if (hasCurrent)
                    curBody.Append(line.Length > 1 ? line[1..] : "");
                continue;
            }

            // New statement — flush previous
            Flush();
            hasCurrent = true;

            // Label is non-blank col1 + any non-space chars up to first space
            if (col1 != ' ' && col1 != '\t')
            {
                int labelEnd = 0;
                while (labelEnd < line.Length && line[labelEnd] != ' ' && line[labelEnd] != '\t')
                    labelEnd++;
                curLabel = line[0..labelEnd].ToUpperInvariant();
                curBody.Append(labelEnd < line.Length ? line[labelEnd..] : "");
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

    private static Stmt ParseBody(string? label, string body)
    {
        var trimmed = body.Trim();

        // END statement
        if (string.Equals(trimmed, "END", StringComparison.OrdinalIgnoreCase) ||
            trimmed.StartsWith("END ", StringComparison.OrdinalIgnoreCase) ||
            trimmed.StartsWith("END\t", StringComparison.OrdinalIgnoreCase))
            return new Stmt { Label = label, IsEnd = true };

        if (string.IsNullOrEmpty(trimmed))
            return new Stmt { Label = label };

        // Parse goto suffix first (peel from right): :(lbl) :S(lbl) :F(lbl)
        var (bodyNoGoto, gotoU, gotoS, gotoF) = ExtractGotos(trimmed);

        // Split body into subject / pattern / = replacement
        var (subjectStr, patternStr, replStr, hasEq) = SplitStatement(bodyNoGoto.Trim());

        Node? subjectNode     = string.IsNullOrEmpty(subjectStr) ? null : ParseExpr(subjectStr);
        Node? patternNode     = string.IsNullOrEmpty(patternStr) ? null : ParseExpr(patternStr);
        Node? replacementNode = (hasEq && !string.IsNullOrEmpty(replStr)) ? ParseExpr(replStr) : null;

        return new Stmt
        {
            Label       = label,
            Subject     = subjectNode,
            Pattern     = patternNode,
            HasEq       = hasEq,
            Replacement = replacementNode,
            GotoU       = gotoU,
            GotoS       = gotoS,
            GotoF       = gotoF,
        };
    }

    // ── Goto extraction ───────────────────────────────────────────────────────
    // Scan right-to-left for :(lbl) :S(lbl) :F(lbl) patterns

    private static (string body, string? u, string? s, string? f)
        ExtractGotos(string line)
    {
        string? gU = null, gS = null, gF = null;
        var work = line;

        // Repeatedly peel goto specs from the right
        bool progress = true;
        while (progress)
        {
            progress = false;
            work = work.TrimEnd();
            if (work.EndsWith(')'))
            {
                int close = work.LastIndexOf(')');
                int open  = work.LastIndexOf('(', close);
                if (open > 0)
                {
                    var inner = work[(open+1)..close].Trim().ToUpperInvariant();
                    var tag   = work[..open].TrimEnd().ToUpperInvariant();
                    if (tag.EndsWith(":S"))      { gS = inner; work = work[..^(work.Length - tag.Length + 2)].TrimEnd(); progress = true; }
                    else if (tag.EndsWith(":F")) { gF = inner; work = work[..^(work.Length - tag.Length + 2)].TrimEnd(); progress = true; }
                    else if (tag.EndsWith(":"))  { gU = inner; work = work[..^(work.Length - tag.Length + 1)].TrimEnd(); progress = true; }
                }
            }
        }
        return (work, gU, gS, gF);
    }

    // ── Statement body splitter ───────────────────────────────────────────────
    // Split:  <subject> <pattern> = <replacement>
    // The split is whitespace-delimited at top level (outside parens/angles/quotes).

    private static (string subject, string pattern, string repl, bool hasEq)
        SplitStatement(string body)
    {
        // Tokenise at top level: collect whitespace-separated tokens, track depth
        var tokens = TopLevelTokens(body);
        if (tokens.Count == 0) return ("", "", "", false);

        string subject = tokens[0];
        string pattern = "";
        string repl    = "";
        bool   hasEq   = false;

        // Find = sign at top level
        int eqIdx = -1;
        for (int i = 1; i < tokens.Count; i++)
        {
            if (tokens[i] == "=") { eqIdx = i; break; }
            if (tokens[i].StartsWith("=") && tokens[i].Length > 1)
            {
                // e.g. "=REPLACE" split oddly — treat as eq + rest
                eqIdx = i;
                tokens[i] = tokens[i][1..];
                break;
            }
        }

        if (eqIdx >= 0)
        {
            hasEq   = true;
            pattern = string.Join(" ", tokens[1..eqIdx]);
            repl    = string.Join(" ", tokens[(eqIdx+1)..]);
        }
        else
        {
            pattern = string.Join(" ", tokens[1..]);
        }

        return (subject.Trim(), pattern.Trim(), repl.Trim(), hasEq);
    }

    private static List<string> TopLevelTokens(string s)
    {
        var tokens = new List<string>();
        var cur    = new System.Text.StringBuilder();
        int depth  = 0;
        bool inStr = false;
        char strCh = '\'';

        void Push() { if (cur.Length > 0) { tokens.Add(cur.ToString()); cur.Clear(); } }

        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (inStr)
            {
                cur.Append(c);
                if (c == strCh) inStr = false;
                continue;
            }
            if (c == '\'' || c == '"') { inStr = true; strCh = c; cur.Append(c); continue; }
            if (c == '(' || c == '<') { depth++; cur.Append(c); continue; }
            if (c == ')' || c == '>') { depth--; cur.Append(c); continue; }
            if ((c == ' ' || c == '\t') && depth == 0) { Push(); continue; }
            cur.Append(c);
        }
        Push();
        return tokens;
    }

    // ── Expression parser ─────────────────────────────────────────────────────
    // Hand-recursive — Pidgin used for the atom/call level.

    public static Node ParseExpr(string src)
    {
        src = src.Trim();
        if (string.IsNullOrEmpty(src)) return new SLit("");

        // Alternation (|) — lowest precedence in pattern context
        var altParts = SplitTopLevel(src, '|');
        if (altParts.Count > 1)
        {
            Node result = ParseConcat(altParts[^1]);
            for (int i = altParts.Count - 2; i >= 0; i--)
                result = new Alt(ParseConcat(altParts[i]), result);
            return result;
        }

        return ParseConcat(src);
    }

    private static Node ParseConcat(string src)
    {
        // Concatenation — space-separated atoms at top level
        var parts = TopLevelTokens(src);
        if (parts.Count == 0) return new SLit("");
        if (parts.Count == 1) return ParseAtom(parts[0]);

        Node result = ParseAtom(parts[^1]);
        for (int i = parts.Count - 2; i >= 0; i--)
            result = new Cat(ParseAtom(parts[i]), result);
        return result;
    }

    private static Node ParseAtom(string src)
    {
        src = src.Trim();
        if (string.IsNullOrEmpty(src)) return new SLit("");

        // Quoted string
        if ((src.StartsWith('\'') && src.EndsWith('\'')) ||
            (src.StartsWith('"')  && src.EndsWith('"')))
            return new SLit(src[1..^1]);

        // @var — cursor assignment
        if (src.StartsWith('@'))
            return new CaptCursor(src[1..].ToUpperInvariant());

        // *expr — deferred/unevaluated pattern
        if (src.StartsWith('*') && src.Length > 1)
            return new DeferredPat(ParseAtom(src[1..]));

        // $expr — indirect reference
        if (src.StartsWith('$') && src.Length > 1)
            return new IndirectRef(ParseAtom(src[1..]));

        // Capture: expr .var or expr $var  — handled at concat level via suffix detection
        // (these appear as separate tokens in TopLevelTokens)

        // Parenthesised group
        if (src.StartsWith('(') && src.EndsWith(')'))
            return ParseExpr(src[1..^1]);

        // Function call or array ref: NAME(args) or NAME<args>
        int pOpen = src.IndexOf('(');
        int aOpen = src.IndexOf('<');
        if (pOpen > 0 && (aOpen < 0 || pOpen < aOpen))
        {
            var name = src[..pOpen].ToUpperInvariant();
            var inner = src[(pOpen+1)..^1];  // strip outer parens
            var argNodes = SplitTopLevel(inner, ',').Select(a => ParseExpr(a)).ToArray();
            return new FncCall(name, argNodes);
        }
        if (aOpen > 0 && src.EndsWith('>'))
        {
            var name  = src[..aOpen];
            var inner = src[(aOpen+1)..^1];
            var idxNodes = SplitTopLevel(inner, ',').Select(a => ParseExpr(a)).ToArray();
            return new ArrayRef(ParseAtom(name), idxNodes);
        }

        // Integer literal
        if (long.TryParse(src, out var ival))
            return new NLit(src);

        // Real literal
        if (double.TryParse(src, System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture, out _))
            return new NLit(src);

        // Variable reference (identifier)
        return new Var(src.ToUpperInvariant());
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static List<string> SplitTopLevel(string s, char sep)
    {
        var parts  = new List<string>();
        var cur    = new System.Text.StringBuilder();
        int depth  = 0;
        bool inStr = false;
        char strCh = '\'';

        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
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
}
