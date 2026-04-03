package bb;

/**
 * bb_rpos.java — RPOS: assert cursor == Ω-n (zero-width)
 * Port of bb_rpos.c / bb_rpos.s
 *
 *   RPOS_α:  if (Δ != Ω-n)    goto RPOS_ω;
 *            RPOS=spec(Σ+Δ,0); goto RPOS_γ;
 *   RPOS_β:                    goto RPOS_ω;
 */
public class bb_rpos extends bb_box {
    private final int n;
    private final java.util.function.IntSupplier dyn;

    public bb_rpos(MatchState ms, int n)                           { super(ms); this.n=n;  this.dyn=null; }
    public bb_rpos(MatchState ms, java.util.function.IntSupplier s) { super(ms); this.n=0;  this.dyn=s; }

    private int val() { return dyn != null ? dyn.getAsInt() : n; }

    @Override public Spec α() {
        if (ms.delta != ms.omega - val()) return null;
        return new Spec(ms.delta, 0);
    }

    @Override public Spec β() { return null; }
}
