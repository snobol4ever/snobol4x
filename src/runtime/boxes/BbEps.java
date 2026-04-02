package snobol4.runtime.boxes;

/**
 * BbEps.java — EPS: zero-width success once; done flag prevents double-γ
 * Port of bb_eps.c / bb_eps.s
 *
 *   EPS_α:  if (done)         goto EPS_ω;
 *           done=1; EPS=spec(Σ+Δ,0); goto EPS_γ;
 *   EPS_β:                    goto EPS_ω;
 */
public class BbEps extends BbBox {
    private boolean done;

    public BbEps(MatchState ms) { super(ms); }

    @Override public Spec alpha() {
        if (done) return null;
        done = true;
        return new Spec(ms.delta, 0);
    }

    @Override public Spec beta() { return null; }
}
