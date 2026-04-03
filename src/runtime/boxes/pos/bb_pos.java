package bb;

/**
 * bb_pos.java — POS: assert cursor == n (zero-width)
 * Port of bb_pos.c / bb_pos.s
 *
 *   POS_α:  if (Δ != n)      goto POS_ω;
 *           POS=spec(Σ+Δ,0); goto POS_γ;
 *   POS_β:                   goto POS_ω;
 */
public class bb_pos extends bb_box {
    private final int n;
    private final java.util.function.IntSupplier dyn; // non-null → dynamic arg

    public bb_pos(MatchState ms, int n)                          { super(ms); this.n=n;  this.dyn=null; }
    public bb_pos(MatchState ms, java.util.function.IntSupplier s) { super(ms); this.n=0;  this.dyn=s; }

    private int val() { return dyn != null ? dyn.getAsInt() : n; }

    @Override public Spec α() {
        int v = val();
        if (ms.delta != v) return null;
        return new Spec(ms.delta, 0);
    }

    @Override public Spec β() { return null; }
}
