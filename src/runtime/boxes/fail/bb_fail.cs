// bb_fail.cs — FAIL: always ω — force backtrack
// Mirrors src/runtime/boxes/fail/bb_fail.c

namespace Snobol4.Runtime.Boxes;

public sealed class bb_fail : IByrdBox
{
    public Spec Alpha(MatchState ms) => Spec.Fail;
    public Spec Beta(MatchState ms)  => Spec.Fail;
}
