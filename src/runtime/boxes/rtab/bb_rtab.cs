// bb_rtab.cs — RTAB: advance cursor TO position Length-n
// Mirrors src/runtime/boxes/rtab/bb_rtab.c
//
// α: if cursor > Length-n → ω;  cursor = Length-n; γ
// β: cursor -= advance; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_rtab : IByrdBox
{
    private readonly int _n;
    private int _advance;

    public bb_rtab(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        int target = ms.Length - _n;
        if (ms.Cursor > target) return Spec.Fail;
        _advance = target - ms.Cursor;
        var result = Spec.Of(ms.Cursor, _advance);
        ms.Cursor = target;
        return result;
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor -= _advance;
        return Spec.Fail;
    }
}
