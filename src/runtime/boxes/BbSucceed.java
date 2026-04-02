/**
 * BbSucceed.java — SUCCEED: always γ zero-width; outer scan loop retries
 * Port of bb_succeed.c / bb_succeed.s
 */
class BbSucceed extends BbBox {

    public BbSucceed(MatchState ms) { super(ms); }

    @Override public Spec alpha() { return new Spec(ms.delta, 0); }
    @Override public Spec beta()  { return new Spec(ms.delta, 0); }
}
