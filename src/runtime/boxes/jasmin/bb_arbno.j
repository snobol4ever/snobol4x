.class public bb/bb_arbno
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private static final MAX_DEPTH I = 64
.field private final body Lbb/bb_box;
.field private final frame_start [I
.field private final frame_match_st [I
.field private final frame_match_ln [I
.field private depth I

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;)V
    .limit stack 4
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_arbno/body Lbb/bb_box;
    aload_0
    bipush 64
    newarray int
    putfield bb/bb_arbno/frame_start [I
    aload_0
    bipush 64
    newarray int
    putfield bb/bb_arbno/frame_match_st [I
    aload_0
    bipush 64
    newarray int
    putfield bb/bb_arbno/frame_match_ln [I
    return
.end method

.method public alpha()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    ; depth=0; frame[0].match_st=delta; frame[0].match_ln=0; frame[0].start=delta
    aload_0
    iconst_0
    putfield bb/bb_arbno/depth I
    aload_0
    getfield bb/bb_arbno/frame_match_st [I
    iconst_0
    aload_0
    getfield bb/bb_arbno/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iastore
    aload_0
    getfield bb/bb_arbno/frame_match_ln [I
    iconst_0
    iconst_0
    iastore
    aload_0
    getfield bb/bb_arbno/frame_start [I
    iconst_0
    aload_0
    getfield bb/bb_arbno/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iastore
    aload_0
    invokevirtual bb/bb_arbno/tryBody()Lbb/bb_box$Spec;
    areturn
.end method

.method public beta()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    ; if depth <= 0 → ω
    aload_0
    getfield bb/bb_arbno/depth I
    ifle arbno_beta_omega
    ; depth--
    aload_0
    dup
    getfield bb/bb_arbno/depth I
    iconst_1
    isub
    putfield bb/bb_arbno/depth I
    ; delta = frame_start[depth]
    aload_0
    getfield bb/bb_arbno/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_arbno/frame_start [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    putfield bb/bb_box$MatchState/delta I
    ; return Spec(frame_match_st[depth], frame_match_ln[depth])
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arbno/frame_match_st [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    aload_0
    getfield bb/bb_arbno/frame_match_ln [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
arbno_beta_omega:
    aconst_null
    areturn
.end method

; private Spec tryBody()
.method private tryBody()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 4
tryBody_loop:
    aload_0
    getfield bb/bb_arbno/body Lbb/bb_box;
    invokevirtual bb/bb_box/alpha()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull tryBody_body_omega
    ; body_γ: check zero-advance guard
    aload_0
    getfield bb/bb_arbno/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_arbno/frame_start [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    if_icmpne tryBody_advance
    ; zero advance: return current accumulated match
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arbno/frame_match_st [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    aload_0
    getfield bb/bb_arbno/frame_match_ln [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
tryBody_advance:
    ; newMatchSt = frame_match_st[depth]; newMatchLn = frame_match_ln[depth] + br.len
    aload_0
    getfield bb/bb_arbno/frame_match_st [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    istore_2                           ; newMatchSt
    aload_0
    getfield bb/bb_arbno/frame_match_ln [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd                               ; newMatchLn on stack
    ; if depth+1 < 64: push frame
    aload_0
    getfield bb/bb_arbno/depth I
    iconst_1
    iadd
    bipush 64
    if_icmpge tryBody_full
    ; depth++
    aload_0
    dup
    getfield bb/bb_arbno/depth I
    iconst_1
    iadd
    putfield bb/bb_arbno/depth I
    ; frame_match_st[depth] = newMatchSt
    aload_0
    getfield bb/bb_arbno/frame_match_st [I
    aload_0
    getfield bb/bb_arbno/depth I
    iload_2
    iastore
    ; frame_match_ln[depth] = newMatchLn (still on stack — need to store)
    istore 3                           ; save newMatchLn to local 3
    aload_0
    getfield bb/bb_arbno/frame_match_ln [I
    aload_0
    getfield bb/bb_arbno/depth I
    iload 3                            ; newMatchLn
    iastore
    ; frame_start[depth] = delta
    aload_0
    getfield bb/bb_arbno/frame_start [I
    aload_0
    getfield bb/bb_arbno/depth I
    aload_0
    getfield bb/bb_arbno/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iastore
    goto tryBody_loop
tryBody_full:
    ; stack full: return Spec(newMatchSt, newMatchLn) — newMatchLn already on stack
    new bb/bb_box$Spec
    dup_x1
    swap
    iload_2
    swap
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
tryBody_body_omega:
    ; body failed: return current accumulated match
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arbno/frame_match_st [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    aload_0
    getfield bb/bb_arbno/frame_match_ln [I
    aload_0
    getfield bb/bb_arbno/depth I
    iaload
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
