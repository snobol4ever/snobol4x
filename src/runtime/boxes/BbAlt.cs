// BbAlt.cs — ALT: alternation — try each child on α; β retries same child
// Mirrors src/runtime/boxes/bb_alt.c
//
// α: save cursor; try children[0].α ... children[n-1].α in order
//    on child_γ: remember which child matched; γ
//    on child_ω: restore cursor, try next child
// β: retry the same child that succeeded on α via child.β
//    on child_γ: γ
//    on child_ω: ω (no cross-child backtrack on β)

namespace Snobol4.Runtime.Boxes;

public sealed class BbAlt : IByrdBox
{
    private readonly IByrdBox[] _children;
    private int  _current;   // 0-based index of child that last fired γ
    private int  _savedPos;

    public BbAlt(params IByrdBox[] children)
    {
        _children = children ?? [];
    }

    public Spec Alpha(MatchState ms)
    {
        _savedPos = ms.Cursor;
        for (int i = 0; i < _children.Length; i++)
        {
            ms.Cursor = _savedPos;                   // restore for each arm
            var cr = _children[i].Alpha(ms);
            if (!cr.IsFail)
            {
                _current = i;
                return cr;                           // child_α_γ → ALT_γ
            }
            // child_α_ω → try next
        }
        ms.Cursor = _savedPos;
        return Spec.Fail;                            // ALT_ω
    }

    public Spec Beta(MatchState ms)
    {
        // β retries the same child only (mirrors bb_alt.c ALT_β)
        var cr = _children[_current].Beta(ms);
        if (!cr.IsFail) return cr;                   // child_β_γ → ALT_γ
        return Spec.Fail;                            // ALT_ω
    }
}
