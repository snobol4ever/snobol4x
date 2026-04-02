package snobol4.runtime.boxes;

/**
 * BbFence.java — FENCE: succeed once; β cuts (no retry)
 * Port of bb_fence.c / bb_fence.s
 *
 *   FENCE_α:  fired=1;            goto FENCE_γ;
 *   FENCE_β:                      goto FENCE_ω;
 *   FENCE_γ:  return spec(Σ+Δ,0);
 *   FENCE_ω:  return spec_empty;
 */
public class BbFence extends BbBox {

    public BbFence(MatchState ms) { super(ms); }

    @Override public Spec alpha() { return new Spec(ms.delta, 0); }  // FENCE_γ

    @Override public Spec beta()  { return null; }                   // FENCE_ω
}
