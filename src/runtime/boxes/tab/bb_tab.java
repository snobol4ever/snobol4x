package bb;

/**
 * bb_tab.java — TAB: advance cursor TO absolute position n
 * Port of bb_tab.c / bb_tab.s
 *
 *   TAB_α:  if (Δ > n)                      goto TAB_ω;
 *           advance=n-Δ; TAB=spec(Σ+Δ,advance); Δ=n;  goto TAB_γ;
 *   TAB_β:  Δ -= advance;                   goto TAB_ω;
 */
public class bb_tab extends bb_box {
    private final int n;
    private final java.util.function.IntSupplier dyn;
    private int       advance;

    public bb_tab(MatchState ms, int n)                           { super(ms); this.n=n;  this.dyn=null; }
    public bb_tab(MatchState ms, java.util.function.IntSupplier s) { super(ms); this.n=0;  this.dyn=s; }

    private int val() { return dyn != null ? dyn.getAsInt() : n; }

    @Override public Spec α() {
        int v = val();
        if (ms.delta > v) return null;
        advance = v - ms.delta;
        Spec r = new Spec(ms.delta, advance);
        ms.delta = v;
        return r;
    }

    @Override public Spec β() { ms.delta -= advance; return null; }
}
