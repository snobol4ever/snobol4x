package bb;

/**
 * bb_len.java — LEN: match exactly n characters
 * Port of bb_len.c / bb_len.s
 *
 *   LEN_α:  if (Δ+n > Ω)            goto LEN_ω;
 *           LEN=spec(Σ+Δ,n); Δ+=n;  goto LEN_γ;
 *   LEN_β:  Δ-=n;                   goto LEN_ω;
 */
public class bb_len extends bb_box {
    private final int n;
    private final java.util.function.IntSupplier dyn;

    public bb_len(MatchState ms, int n)                           { super(ms); this.n=n;  this.dyn=null; }
    public bb_len(MatchState ms, java.util.function.IntSupplier s) { super(ms); this.n=0;  this.dyn=s; }

    private int val() { return dyn != null ? dyn.getAsInt() : n; }

    @Override public Spec α() {
        int v = val();
        if (ms.delta + v > ms.omega) return null;
        Spec r = new Spec(ms.delta, v);
        ms.delta += v;
        return r;
    }

    @Override public Spec β() { ms.delta -= val(); return null; }
}
