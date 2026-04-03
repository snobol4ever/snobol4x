// bb_not.cs — NOT: \X — succeed iff child fails; β always ω
// Mirrors src/runtime/boxes/bb_not.c
//
// α: save cursor; run child.α
//    child_γ → restore cursor; NOT_ω  (child succeeded → we fail)
//    child_ω → restore cursor; NOT_γ zero-width (child failed → we succeed)
// β: ω (negation succeeds at most once per position)

namespace Snobol4.Runtime.Boxes;

public sealed class bb_not : IByrdBox
{
    private readonly IByrdBox _child;

    public bb_not(IByrdBox child) { _child = child; }

    public Spec Alpha(MatchState ms)
    {
        int saved = ms.Cursor;
        var cr = _child.Alpha(ms);
        ms.Cursor = saved;                            // always restore

        if (!cr.IsFail) return Spec.Fail;             // child_γ → NOT_ω
        return Spec.ZeroWidth(ms.Cursor);             // child_ω → NOT_γ
    }

    public Spec Beta(MatchState ms) => Spec.Fail;    // NOT_ω
}
