package bb;

/**
 * bb_seq.java — SEQ: concatenation; left then right; β retries right then left
 * Port of bb_seq.c / bb_seq.s
 *
 * C original:
 *   SEQ_α:    matched=spec(Σ+Δ,0);
 *             lr = left(α);   if empty → left_ω;  else → left_γ;
 *   SEQ_β:    rr = right(β);  if empty → right_ω; else → right_γ;
 *   left_γ:   matched = cat(matched, lr);
 *             rr = right(α);  if empty → right_ω; else → right_γ;
 *   left_ω:                                        goto SEQ_ω;
 *   right_γ:  SEQ = cat(matched, rr);              goto SEQ_γ;
 *   right_ω:  lr = left(β);   if empty → left_ω;  else → left_γ;
 *   SEQ_γ:                                         return SEQ;
 *   SEQ_ω:                                         return spec_empty;
 */
public class bb_seq extends bb_box {
    private final bb_box left, right;
    private int matchedStart, matchedLen;  /* accumulated span */

    public bb_seq(MatchState ms, bb_box left, bb_box right) {
        super(ms);
        this.left  = left;
        this.right = right;
    }

    @Override public Spec α() {
        // SEQ_α
        matchedStart = ms.delta;
        matchedLen   = 0;
        Spec lr = left.α();
        if (lr == null) return null;                                   // left_ω → SEQ_ω
        // left_γ
        matchedLen += lr.len;
        return rightAlpha();
    }

    @Override public Spec β() {
        // SEQ_β: retry right first
        Spec rr = right.β();
        if (rr != null) return new Spec(matchedStart, matchedLen + rr.len); // right_γ
        // right_ω: backtrack left
        return leftBeta();
    }

    /* right_ω path: try left.β then right.α */
    private Spec leftBeta() {
        Spec lr = left.β();
        if (lr == null) return null;                                   // left_ω → SEQ_ω
        // left_γ
        matchedLen = lr.len;
        return rightAlpha();
    }

    private Spec rightAlpha() {
        Spec rr = right.α();
        if (rr == null) return leftBeta();                             // right_ω
        return new Spec(matchedStart, matchedLen + rr.len);           // right_γ → SEQ_γ
    }
}
