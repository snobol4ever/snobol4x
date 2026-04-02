// BbEps.cs — EPS: zero-width success once; done flag prevents double-γ
// Mirrors src/runtime/boxes/bb_eps.c
//
// α: if done → ω;  done=true; γ zero-width
// β: ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbEps : IByrdBox
{
    private bool _done;

    public Spec Alpha(MatchState ms)
    {
        if (_done) return Spec.Fail;
        _done = true;
        return Spec.ZeroWidth(ms.Cursor);
    }

    public Spec Beta(MatchState ms) => Spec.Fail;
}
