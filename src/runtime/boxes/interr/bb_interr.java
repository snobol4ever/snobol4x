package bb;

/**
 * bb_interr.java — INTERR: ?X — succeed zero-width if X succeeds; ω if X fails
 * Port of bb_interr.c / bb_interr.s
 *
 * o$int semantics: run child; if child γ → discard match, return zero-width
 * at the *original* cursor; if child ω → propagate ω.
 * β: unconditional ω — interrogation succeeds at most once per position.
 *
 *   INT_α:  start=Δ; cr=child(α); if empty → INT_ω;
 *           Δ=start; goto INT_γ;
 *   INT_β:            goto INT_ω;
 *   INT_γ:  return spec(Σ+Δ,0);
 *   INT_ω:  return spec_empty;
 */
public class bb_interr extends bb_box {
    private final bb_box child;
    private int         start;

    public bb_interr(MatchState ms, bb_box child) { super(ms); this.child=child; }

    @Override public Spec α() {
        start    = ms.delta;
        Spec cr  = child.α();
        if (cr == null) return null;                                           // INT_ω
        ms.delta = start;                                                      // discard advance
        return new Spec(ms.delta, 0);                                          // INT_γ zero-width
    }

    @Override public Spec β() { return null; }                              // INT_ω
}
