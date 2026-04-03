package bb;

/**
 * bb_rem.java — REM: match entire remainder; no backtrack
 * Port of bb_rem.c / bb_rem.s
 *
 *   REM_α:  REM=spec(Σ+Δ, Ω-Δ); Δ=Ω;  goto REM_γ;
 *   REM_β:                              goto REM_ω;
 */
public class bb_rem extends bb_box {

    public bb_rem(MatchState ms) { super(ms); }

    @Override public Spec α() {
        Spec r = new Spec(ms.delta, ms.omega - ms.delta);
        ms.delta = ms.omega;
        return r;
    }

    @Override public Spec β() { return null; }
}
