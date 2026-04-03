// bb_capture.cs — CAPTURE: $ writes on every γ; . buffers for Phase-5 commit
// Mirrors src/runtime/boxes/bb_capture.c
//
// Wraps a child box.  On every γ (α or β):
//   immediate=true  ($var): write matched substring to variable NOW
//   immediate=false (.var): buffer pending capture; Phase 5 commits on :S
//
// On ω: clear pending.
//
// The VariableStore delegate matches snobol4dotnet's IdentifierTable write path.

namespace Snobol4.Runtime.Boxes;

public sealed class bb_capture : IByrdBox
{
    private readonly IByrdBox _child;
    private readonly string   _varname;
    private readonly bool     _immediate;

    // Injected by the executor — writes varname=value into IdentifierTable
    public  Action<string, string>? SetVar    { get; set; }
    // Pending capture set on every γ; flushed by Phase 5 on :S
    public  string?                 Pending   { get; private set; }
    public  bool                    HasPending { get; private set; }

    public bb_capture(IByrdBox child, string varname, bool immediate)
    {
        _child     = child;
        _varname   = varname ?? "";
        _immediate = immediate;
    }

    public Spec Alpha(MatchState ms) => TryChild(ms, alpha: true);
    public Spec Beta(MatchState ms)  => TryChild(ms, alpha: false);

    private Spec TryChild(MatchState ms, bool alpha)
    {
        var cr = alpha ? _child.Alpha(ms) : _child.Beta(ms);
        if (cr.IsFail)
        {
            HasPending = false;
            return Spec.Fail;                                       // CAP_ω
        }

        // γ path — extract matched substring
        string matched = ms.Subject.Substring(cr.Start, cr.Length);

        if (_immediate && !string.IsNullOrEmpty(_varname))
        {
            SetVar?.Invoke(_varname, matched);                      // $ immediate write
        }
        else if (!string.IsNullOrEmpty(_varname))
        {
            Pending    = matched;                                   // . deferred write
            HasPending = true;
        }

        return cr;                                                  // CAP_γ
    }

    // Called by Phase 5 on :S to commit pending conditional captures
    public void CommitPending()
    {
        if (HasPending && !string.IsNullOrEmpty(_varname))
            SetVar?.Invoke(_varname, Pending ?? "");
        HasPending = false;
    }
}
