// bb_tab.cs — TAB: advance cursor TO absolute position n
// Mirrors src/runtime/boxes/tab/bb_tab.c
//
// α: if cursor > n → ω;  advance = n-cursor; cursor = n; γ
// β: cursor -= advance; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_tab : IByrdBox
{
    private readonly int _n;
    private int _advance;

    public bb_tab(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor > _n) return Spec.Fail;
        _advance = _n - ms.Cursor;
        var result = Spec.Of(ms.Cursor, _advance);
        ms.Cursor = _n;
        return result;
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor -= _advance;
        return Spec.Fail;
    }
}
