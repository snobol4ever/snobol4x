// bb_any.cs — ANY: match one char if in charset
// Mirrors src/runtime/boxes/any/bb_any.c
//
// α: if cursor >= Length or char not in charset → ω;  cursor++; γ(1 char)
// β: cursor--; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_any : IByrdBox
{
    private readonly string _chars;
    public bb_any(string chars) { _chars = chars ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor >= ms.Length)           return Spec.Fail;
        if (!ms.CharInSet(ms.Cursor, _chars)) return Spec.Fail;
        var result = Spec.Of(ms.Cursor, 1);
        ms.Cursor++;
        return result;
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor--;
        return Spec.Fail;
    }
}
