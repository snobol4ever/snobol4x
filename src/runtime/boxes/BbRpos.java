package snobol4.runtime.boxes;

/**
 * BbRpos.java — RPOS: assert cursor == Ω-n (zero-width)
 * Port of bb_rpos.c / bb_rpos.s
 *
 *   RPOS_α:  if (Δ != Ω-n)    goto RPOS_ω;
 *            RPOS=spec(Σ+Δ,0); goto RPOS_γ;
 *   RPOS_β:                    goto RPOS_ω;
 */
public class BbRpos extends BbBox {
    private final int n;

    public BbRpos(MatchState ms, int n) { super(ms); this.n=n; }

    @Override public Spec alpha() {
        if (ms.delta != ms.omega - n) return null;
        return new Spec(ms.delta, 0);
    }

    @Override public Spec beta() { return null; }
}
