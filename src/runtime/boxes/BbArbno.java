/**
 * BbArbno.java — ARBNO: zero-or-more greedy; zero-advance guard; β unwinds stack
 * Port of bb_arbno.c / bb_arbno.s
 *
 *   ARBNO_α:   depth=0; fr=stack[0]; fr.matched=spec(Σ+Δ,0); fr.start=Δ;
 *   ARBNO_try: br = body(α);  if empty → body_ω;  else → body_γ;
 *   ARBNO_β:   if (depth<=0)                       goto ARBNO_ω;
 *              depth--; fr=stack[depth]; Δ=fr.start; goto ARBNO_γ;
 *   body_γ:    fr=stack[depth];
 *              if (Δ==fr.start)                    goto ARBNO_γ_now;  (zero-advance guard)
 *              ARBNO=cat(fr.matched, br);
 *              if room: depth++; fr=stack[depth]; fr.matched=ARBNO; fr.start=Δ;
 *                       goto ARBNO_try;
 *   body_ω:    ARBNO=stack[depth].matched;         goto ARBNO_γ;
 *   ARBNO_γ_now: ARBNO=stack[depth].matched;       goto ARBNO_γ;
 *   ARBNO_γ:                                       return ARBNO;
 *   ARBNO_ω:                                       return spec_empty;
 */
class BbArbno extends BbBox {
    private static final int MAX_DEPTH = 64;

    private final BbBox body;

    /* per-frame saved state */
    private final int[] frameStart   = new int[MAX_DEPTH];
    private final int[] frameMatchSt = new int[MAX_DEPTH]; /* start of accumulated match */
    private final int[] frameMatchLn = new int[MAX_DEPTH]; /* length of accumulated match */
    private int depth;

    public BbArbno(MatchState ms, BbBox body) { super(ms); this.body=body; }

    @Override public Spec alpha() {
        // ARBNO_α
        depth = 0;
        frameMatchSt[0] = ms.delta;
        frameMatchLn[0] = 0;
        frameStart[0]   = ms.delta;
        return tryBody();
    }

    @Override public Spec beta() {
        // ARBNO_β: unwind one stack frame
        if (depth <= 0) return null;                                           // ARBNO_ω
        depth--;
        ms.delta = frameStart[depth];
        return new Spec(frameMatchSt[depth], frameMatchLn[depth]);             // ARBNO_γ
    }

    private Spec tryBody() {
        while (true) {
            Spec br = body.alpha();
            if (br == null) {
                // body_ω: stop looping, return current accumulated match
                return new Spec(frameMatchSt[depth], frameMatchLn[depth]);     // ARBNO_γ
            }
            // body_γ
            int savedStart = frameStart[depth];
            if (ms.delta == savedStart) {
                // zero-advance guard: don't push, return now
                return new Spec(frameMatchSt[depth], frameMatchLn[depth]);     // ARBNO_γ_now
            }
            int newMatchSt = frameMatchSt[depth];
            int newMatchLn = frameMatchLn[depth] + br.len;
            if (depth + 1 < MAX_DEPTH) {
                depth++;
                frameMatchSt[depth] = newMatchSt;
                frameMatchLn[depth] = newMatchLn;
                frameStart[depth]   = ms.delta;
                // ARBNO_try: loop
            } else {
                // stack full: accept current
                return new Spec(newMatchSt, newMatchLn);                       // ARBNO_γ
            }
        }
    }
}
