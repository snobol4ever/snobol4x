// BbBal.cs — BAL: match a balanced-parentheses expression
// No direct C box (BAL is a primitive in SNOBOL4 spec).
// Semantics: match shortest string that is balanced with respect to
// parentheses '(' and ')'. No backtrack — matches exactly one balanced
// token at the current position.
//
// α: scan from cursor; count parens; succeed when depth returns to 0
//    and at least one char is consumed.
// β: ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbBal : IByrdBox
{
    private int _matchedLen;

    public Spec Alpha(MatchState ms)
    {
        int pos   = ms.Cursor;
        int depth = 0;
        int len   = ms.Length;
        string s  = ms.Subject;

        if (pos >= len) return Spec.Fail;

        int start = pos;
        do
        {
            char c = s[pos];
            if      (c == '(') depth++;
            else if (c == ')')
            {
                if (depth == 0) return Spec.Fail;   // unmatched close
                depth--;
            }
            pos++;
        }
        while (pos < len && depth > 0);

        if (depth != 0) return Spec.Fail;            // unclosed open

        _matchedLen = pos - start;
        var result  = Spec.Of(start, _matchedLen);
        ms.Cursor   = pos;
        return result;
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor -= _matchedLen;
        return Spec.Fail;
    }
}
