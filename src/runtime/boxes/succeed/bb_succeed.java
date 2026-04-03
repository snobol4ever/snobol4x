package bb;

/**
 * bb_succeed.java — SUCCEED: always γ zero-width; outer scan loop retries
 * Port of bb_succeed.c / bb_succeed.s
 */
public class bb_succeed extends bb_box {

    public bb_succeed(MatchState ms) { super(ms); }

    @Override public Spec α() { return new Spec(ms.delta, 0); }
    @Override public Spec β()  { return new Spec(ms.delta, 0); }
}
