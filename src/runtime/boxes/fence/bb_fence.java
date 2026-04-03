package bb;

/**
 * bb_fence.java — FENCE: succeed once; β cuts (no retry)
 * Port of bb_fence.c / bb_fence.s
 *
 *   FENCE_α:  fired=1;            goto FENCE_γ;
 *   FENCE_β:                      goto FENCE_ω;
 *   FENCE_γ:  return spec(Σ+Δ,0);
 *   FENCE_ω:  return spec_empty;
 */
public class bb_fence extends bb_box {

    public bb_fence(MatchState ms) { super(ms); }

    @Override public Spec α() { return new Spec(ms.delta, 0); }  // FENCE_γ

    @Override public Spec β()  { return null; }                   // FENCE_ω
}
