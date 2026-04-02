// BbInterr.cs — INTERR: ?X — zero-width if child succeeds; ω if child fails
// Mirrors src/runtime/boxes/bb_interr.c
//
// The interrogation operator (o$int): run child; if child γ → discard the
// match advance, return zero-width at the ORIGINAL cursor (null string result).
// If child ω → propagate ω.
// β: ω — interrogation succeeds at most once.

namespace Snobol4.Runtime.Boxes;

public sealed class BbInterr : IByrdBox
{
    private readonly IByrdBox _child;

    public BbInterr(IByrdBox child) { _child = child; }

    public Spec Alpha(MatchState ms)
    {
        int saved = ms.Cursor;
        var cr = _child.Alpha(ms);
        ms.Cursor = saved;                            // always restore cursor

        if (cr.IsFail) return Spec.Fail;              // child_ω → INT_ω
        return Spec.ZeroWidth(ms.Cursor);             // child_γ → INT_γ (zero-width)
    }

    public Spec Beta(MatchState ms) => Spec.Fail;    // INT_ω
}
