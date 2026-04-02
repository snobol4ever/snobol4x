package snobol4.runtime.boxes;

/**
 * BbNot.java — NOT: \X — succeed iff X fails; β always ω
 * Port of bb_not.c / bb_not.s
 *
 * o$nta/b/c semantics mapped to two-entry BB:
 *   α: run child; if child γ → NOT_ω (child succeeded → we fail);
 *                  if child ω → NOT_γ zero-width at original cursor
 *   β: unconditional NOT_ω — negation succeeds at most once per position
 *
 *   NOT_α:  start=Δ; cr=child(α); if !empty → NOT_ω;
 *           Δ=start; goto NOT_γ;
 *   NOT_β:  goto NOT_ω;
 *   NOT_γ:  return spec(Σ+Δ,0);
 *   NOT_ω:  return spec_empty;
 */
public class BbNot extends BbBox {
    private final BbBox child;
    private int         start;

    public BbNot(MatchState ms, BbBox child) { super(ms); this.child=child; }

    @Override public Spec alpha() {
        start   = ms.delta;
        Spec cr = child.alpha();
        if (cr != null) return null;                                           // NOT_ω (child succeeded)
        ms.delta = start;
        return new Spec(ms.delta, 0);                                          // NOT_γ
    }

    @Override public Spec beta() { return null; }                              // NOT_ω
}
