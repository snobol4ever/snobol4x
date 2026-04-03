// bb_fence.cs — FENCE: succeed once; β cuts (no retry)
// Mirrors src/runtime/boxes/bb_fence.c
//
// α: γ zero-width (always succeeds first time)
// β: ω (cut — no retry)

namespace Snobol4.Runtime.Boxes;

public sealed class bb_fence : IByrdBox
{
    public Spec Alpha(MatchState ms) => Spec.ZeroWidth(ms.Cursor);
    public Spec Beta(MatchState ms)  => Spec.Fail;
}

// bb_abort.cs — ABORT: always ω regardless of entry
// Mirrors src/runtime/boxes/bb_abort.c

public sealed class bb_abort : IByrdBox
{
    public Spec Alpha(MatchState ms) => Spec.Fail;
    public Spec Beta(MatchState ms)  => Spec.Fail;
}

// bb_fail.cs — FAIL: always ω — force backtrack
// Mirrors src/runtime/boxes/bb_fail.c

public sealed class bb_fail : IByrdBox
{
    public Spec Alpha(MatchState ms) => Spec.Fail;
    public Spec Beta(MatchState ms)  => Spec.Fail;
}

// bb_succeed.cs — SUCCEED: always γ zero-width; outer scan loop retries
// Mirrors src/runtime/boxes/bb_succeed.c
//
// Always succeeds; the scan loop's retry-on-backtrack produces the
// "try-everything" effect SNOBOL4's SUCCEED provides.

public sealed class bb_succeed : IByrdBox
{
    public Spec Alpha(MatchState ms) => Spec.ZeroWidth(ms.Cursor);
    public Spec Beta(MatchState ms)  => Spec.ZeroWidth(ms.Cursor);
}
