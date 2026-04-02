/**
 * BbFail.java — FAIL: always ω — force backtrack
 * Port of bb_fail.c / bb_fail.s
 */
class BbFail extends BbBox {

    public BbFail(MatchState ms) { super(ms); }

    @Override public Spec alpha() { return null; }
    @Override public Spec beta()  { return null; }
}
