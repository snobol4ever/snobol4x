package bb;

/**
 * bb_rtab.java — RTAB: advance cursor TO position Ω-n
 * Port of bb_rtab.c / bb_rtab.s
 *
 *   RTAB_α:  if (Δ > Ω-n)                               goto RTAB_ω;
 *            advance=(Ω-n)-Δ; RTAB=spec(Σ+Δ,advance); Δ=Ω-n;  goto RTAB_γ;
 *   RTAB_β:  Δ -= advance;                              goto RTAB_ω;
 */
public class bb_rtab extends bb_box {
    private final int n;
    private final java.util.function.IntSupplier dyn;
    private int       advance;

    public bb_rtab(MatchState ms, int n)                           { super(ms); this.n=n;  this.dyn=null; }
    public bb_rtab(MatchState ms, java.util.function.IntSupplier s) { super(ms); this.n=0;  this.dyn=s; }

    private int val() { return dyn != null ? dyn.getAsInt() : n; }

    @Override public Spec α() {
        int target = ms.omega - val();
        if (ms.delta > target) return null;
        advance = target - ms.delta;
        Spec r = new Spec(ms.delta, advance);
        ms.delta = target;
        return r;
    }

    @Override public Spec β() { ms.delta -= advance; return null; }
}
