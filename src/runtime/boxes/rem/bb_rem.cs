// bb_rem.cs — REM: match entire remainder; no backtrack
// Mirrors src/runtime/boxes/bb_rem.c
//
// α: result = spec(cursor, Length-cursor); cursor = Length; γ
// β: ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_rem : IByrdBox
{
    public Spec Alpha(MatchState ms)
    {
        var result = Spec.Of(ms.Cursor, ms.Length - ms.Cursor);
        ms.Cursor  = ms.Length;
        return result;
    }

    public Spec Beta(MatchState ms) => Spec.Fail;
}
