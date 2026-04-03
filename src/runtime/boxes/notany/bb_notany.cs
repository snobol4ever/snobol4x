// bb_notany.cs — NOTANY: match one char if NOT in charset
// Mirrors src/runtime/boxes/notany/bb_notany.c
//
// α: if cursor >= Length or char in charset → ω;  cursor++; γ(1 char)
// β: cursor--; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_notany : IByrdBox
{
    private readonly string _chars;
    public bb_notany(string chars) { _chars = chars ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor >= ms.Length)          return Spec.Fail;
        if (ms.CharInSet(ms.Cursor, _chars)) return Spec.Fail;
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
