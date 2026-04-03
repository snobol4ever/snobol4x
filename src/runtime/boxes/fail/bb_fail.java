package bb;

/**
 * bb_fail.java — FAIL: always ω — force backtrack
 * Port of bb_fail.c / bb_fail.s
 */
public class bb_fail extends bb_box {

    public bb_fail(MatchState ms) { super(ms); }

    @Override public Spec α() { return null; }
    @Override public Spec β()  { return null; }
}
