// bb_fence.cs — FENCE: succeed once; β cuts (no retry)
// Mirrors src/runtime/boxes/fence/bb_fence.c
//
// α: γ zero-width (always succeeds first time)
// β: ω (cut — no retry)

namespace Snobol4.Runtime.Boxes;

public sealed class bb_fence : IByrdBox
{
    public Spec Alpha(MatchState ms) => Spec.ZeroWidth(ms.Cursor);
    public Spec Beta(MatchState ms)  => Spec.Fail;
}
