// BbTab.cs — TAB: advance cursor TO absolute position n
// Mirrors src/runtime/boxes/bb_tab.c
//
// α: if cursor > n → ω;  advance = n-cursor; cursor = n; γ
// β: cursor -= advance; ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbTab : IByrdBox
{
    private readonly int _n;
    private int _advance;

    public BbTab(int n) { _n = n; }

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

// BbRtab.cs — RTAB: advance cursor TO position Length-n
// Mirrors src/runtime/boxes/bb_rtab.c

public sealed class BbRtab : IByrdBox
{
    private readonly int _n;
    private int _advance;

    public BbRtab(int n) { _n = n; }

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
