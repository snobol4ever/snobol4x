package bb;

/**
 * bb_alt.java — ALT: alternation; try each child on α; β retries same child
 * Port of bb_alt.c / bb_alt.s
 *
 * C original:
 *   ALT_α:      position=Δ; current=1;
 *               cr = children[0](α); if empty → child_α_ω; else → child_α_γ;
 *   ALT_β:      cr = children[current-1](β);
 *               if empty → ALT_ω; else → child_β_γ;
 *   child_α_γ:  result=cr;                             goto ALT_γ;
 *   child_α_ω:  current++;
 *               if current > n                         goto ALT_ω;
 *               Δ=position;
 *               cr = children[current-1](α); if empty → child_α_ω; else → child_α_γ;
 *   child_β_γ:  result=cr;                             goto ALT_γ;
 *   ALT_γ:                                             return result;
 *   ALT_ω:                                             return spec_empty;
 */
public class bb_alt extends bb_box {
    private final bb_box[] children;
    private final int     n;
    private int           current;   /* 1-based index of active child */
    private int           position;  /* saved Δ at ALT entry          */

    public bb_alt(MatchState ms, bb_box... children) {
        super(ms);
        this.children = children;
        this.n        = children.length;
    }

    @Override public Spec α() {
        // ALT_α
        position = ms.delta;
        current  = 1;
        return tryα();
    }

    @Override public Spec β() {
        // ALT_β: retry same child with β
        Spec cr = children[current - 1].β();
        if (cr == null) return null;                                   // ALT_ω
        return cr;                                                     // child_β_γ → ALT_γ
    }

    /* child_α_ω loop: advance to next child */
    private Spec tryα() {
        while (current <= n) {
            ms.delta = position;
            Spec cr  = children[current - 1].α();
            if (cr != null) return cr;                                 // child_α_γ → ALT_γ
            current++;                                                 // child_α_ω
        }
        return null;                                                   // ALT_ω
    }
}
