package bb;

/**
 * bb_bal.java — BAL: match a balanced parenthesised string
 *
 * No bb_bal.c reference exists; semantics from SPITBOL/SNOBOL4 spec:
 *   Matches a string balanced with respect to '(' and ')'.
 *   Scans forward consuming chars; tracks depth. Succeeds at first
 *   position where depth returns to 0 after at least one char consumed.
 *   β: unconditional ω (BAL does not backtrack to shorter match).
 *
 *   BAL_α:  depth=0; len=0;
 *           scan: while (Δ+len < Ω):
 *             ch = Σ[Δ+len]
 *             if ch=='(' depth++
 *             else if ch==')' { if depth==0 → break; depth-- }
 *             len++;
 *             if depth==0 → BAL=spec(Σ+Δ,len); Δ+=len; goto BAL_γ;
 *           goto BAL_ω;
 *   BAL_β:  Δ -= len;  goto BAL_ω;
 */
public class bb_bal extends bb_box {
    private int len;   /* saved match length for β restore */

    public bb_bal(MatchState ms) { super(ms); }

    @Override public Spec α() {
        int depth = 0;
        len = 0;
        while (ms.delta + len < ms.omega) {
            char ch = ms.sigma.charAt(ms.delta + len);
            if      (ch == '(')              { depth++; len++; }
            else if (ch == ')' && depth > 0) { depth--; len++; }
            else if (ch == ')' && depth == 0){ break; }         /* unmatched ')' */
            else                             { len++; }
            if (depth == 0 && len > 0) {
                Spec r = new Spec(ms.delta, len);
                ms.delta += len;
                return r;                                                      // BAL_γ
            }
        }
        return null;                                                           // BAL_ω
    }

    @Override public Spec β() { ms.delta -= len; return null; }            // BAL_ω
}
