; bb_boxes.j — All Byrd box Jasmin JVM implementations, consolidated
; AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

; ───── lit ─────
.class public bb/bb_lit
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box

.field private final lit Ljava/lang/String;
.field private final len I

.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_lit/lit Ljava/lang/String;
    aload_0
    aload_2
    invokevirtual java/lang/String/length()I
    putfield bb/bb_lit/len I
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/len I
    iadd
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpgt lit_omega
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/lit Ljava/lang/String;
    iconst_0
    aload_0
    getfield bb/bb_lit/len I
    invokevirtual java/lang/String/regionMatches(ILjava/lang/String;II)Z
    ifeq lit_omega
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    aload_0
    getfield bb/bb_lit/len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
lit_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── seq ─────
.class public bb/bb_seq
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final left Lbb/bb_box;
.field private final right Lbb/bb_box;
.field private matched_start I
.field private matched_len I

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;Lbb/bb_box;)V
    .limit stack 3
    .limit locals 4
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_seq/left Lbb/bb_box;
    aload_0
    aload_3
    putfield bb/bb_seq/right Lbb/bb_box;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    ; matched_start = delta; matched_len = 0
    aload_0
    aload_0
    getfield bb/bb_seq/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_seq/matched_start I
    aload_0
    iconst_0
    putfield bb/bb_seq/matched_len I
    ; lr = left.α()
    aload_0
    getfield bb/bb_seq/left Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull seq_omega
    ; matched_len += lr.len
    aload_0
    dup
    getfield bb/bb_seq/matched_len I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    putfield bb/bb_seq/matched_len I
    ; tail call rightAlpha
    aload_0
    invokevirtual bb/bb_seq/rightAlpha()Lbb/bb_box$Spec;
    areturn
seq_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    ; rr = right.β()
    aload_0
    getfield bb/bb_seq/right Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull seq_beta_right_omega
    ; return Spec(matched_start, matched_len + rr.len)
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_seq/matched_start I
    aload_0
    getfield bb/bb_seq/matched_len I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
seq_beta_right_omega:
    aload_0
    invokevirtual bb/bb_seq/leftBeta()Lbb/bb_box$Spec;
    areturn
.end method

; private Spec rightAlpha()
.method private rightAlpha()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_seq/right Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull rightAlpha_omega
    ; return Spec(matched_start, matched_len + rr.len)
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_seq/matched_start I
    aload_0
    getfield bb/bb_seq/matched_len I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
rightAlpha_omega:
    aload_0
    invokevirtual bb/bb_seq/leftBeta()Lbb/bb_box$Spec;
    areturn
.end method

; private Spec leftBeta()
.method private leftBeta()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_seq/left Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull leftBeta_omega
    ; matched_len = lr.len
    aload_0
    aload_1
    getfield bb/bb_box$Spec/len I
    putfield bb/bb_seq/matched_len I
    aload_0
    invokevirtual bb/bb_seq/rightAlpha()Lbb/bb_box$Spec;
    areturn
leftBeta_omega:
    aconst_null
    areturn
.end method

; ───── alt ─────
.class public bb/bb_alt
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final children [Lbb/bb_box;
.field private final n I
.field private current I
.field private position I

.method public <init>(Lbb/bb_box$MatchState;[Lbb/bb_box;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_alt/children [Lbb/bb_box;
    aload_0
    aload_2
    arraylength
    putfield bb/bb_alt/n I
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 3
    .limit locals 1
    ; position = delta; current = 1
    aload_0
    aload_0
    getfield bb/bb_alt/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_alt/position I
    aload_0
    iconst_1
    putfield bb/bb_alt/current I
    ; fall through to tryα loop
    aload_0
    invokevirtual bb/bb_alt/tryα()Lbb/bb_box$Spec;
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 2
    ; retry same child with β
    aload_0
    getfield bb/bb_alt/children [Lbb/bb_box;
    aload_0
    getfield bb/bb_alt/current I
    iconst_1
    isub
    aaload                              ; children[current-1]
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull alt_β_omega
    aload_1
    areturn
alt_β_omega:
    aconst_null
    areturn
.end method

; private Spec tryα()
.method private tryα()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 2
try_α_loop:
    ; while current <= n
    aload_0
    getfield bb/bb_alt/current I
    aload_0
    getfield bb/bb_alt/n I
    if_icmpgt try_α_omega
    ; delta = position
    aload_0
    getfield bb/bb_alt/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_alt/position I
    putfield bb/bb_box$MatchState/delta I
    ; cr = children[current-1].α()
    aload_0
    getfield bb/bb_alt/children [Lbb/bb_box;
    aload_0
    getfield bb/bb_alt/current I
    iconst_1
    isub
    aaload
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull try_α_next
    aload_1
    areturn
try_α_next:
    ; current++
    aload_0
    dup
    getfield bb/bb_alt/current I
    iconst_1
    iadd
    putfield bb/bb_alt/current I
    goto try_α_loop
try_α_omega:
    aconst_null
    areturn
.end method

; ───── arb ─────
.class public bb/bb_arb
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private arb_count I
.field private arb_start I
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    ; count=0; start=delta; return Spec(delta,0)
    aload_0
    iconst_0
    putfield bb/bb_arb/arb_count I
    aload_0
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_arb/arb_start I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    ; count++
    aload_0
    dup
    getfield bb/bb_arb/arb_count I
    iconst_1
    iadd
    putfield bb/bb_arb/arb_count I
    ; if start+count > omega → ω
    aload_0
    getfield bb/bb_arb/arb_start I
    aload_0
    getfield bb/bb_arb/arb_count I
    iadd
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpgt arb_omega
    ; delta = start; return Spec(delta, count); delta += count
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_arb/arb_start I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_arb/arb_count I
    invokespecial bb/bb_box$Spec/<init>(II)V
    ; delta += count
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_arb/arb_count I
    iadd
    putfield bb/bb_box$MatchState/delta I
    areturn
arb_omega:
    aconst_null
    areturn
.end method

; ───── arbno ─────
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

.method public α()Lbb/bb_box$Spec;
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

.method public β()Lbb/bb_box$Spec;
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
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
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

; ───── any ─────
.class public bb/bb_any
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_any/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge any_omega
    aload_0
    getfield bb/bb_any/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    iflt any_omega
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    iconst_1
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    iconst_1
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
any_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_any/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    iconst_1
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── notany ─────
.class public bb/bb_notany
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_notany/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge notany_omega
    aload_0
    getfield bb/bb_notany/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    ifge notany_omega
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    iconst_1
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    iconst_1
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
notany_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    iconst_1
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── span ─────
.class public bb/bb_span
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.field private matched_len I
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_span/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 3
    ; matched_len = 0
    aload_0
    iconst_0
    putfield bb/bb_span/matched_len I
span_loop:
    ; while delta+matched_len < omega && chars.indexOf(sigma.charAt(delta+matched_len))>=0
    aload_0
    getfield bb/bb_span/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_span/matched_len I
    iadd
    istore_1                              ; pos = delta + matched_len
    iload_1
    aload_0
    getfield bb/bb_span/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge span_done
    aload_0
    getfield bb/bb_span/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_span/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    iload_1
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    iflt span_done
    aload_0
    dup
    getfield bb/bb_span/matched_len I
    iconst_1
    iadd
    putfield bb/bb_span/matched_len I
    goto span_loop
span_done:
    aload_0
    getfield bb/bb_span/matched_len I
    ifle span_omega
    aload_0
    getfield bb/bb_span/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_2
    aload_0
    getfield bb/bb_span/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_span/matched_len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_2
    aload_0
    getfield bb/bb_span/matched_len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
span_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_span/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_span/matched_len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── brk ─────
.class public bb/bb_brk
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.field private matched_len I
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_brk/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 3
    aload_0
    iconst_0
    putfield bb/bb_brk/matched_len I
brk_loop:
    aload_0
    getfield bb/bb_brk/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_brk/matched_len I
    iadd
    istore_1
    iload_1
    aload_0
    getfield bb/bb_brk/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge brk_omega
    aload_0
    getfield bb/bb_brk/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_brk/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    iload_1
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    ifge brk_found
    aload_0
    dup
    getfield bb/bb_brk/matched_len I
    iconst_1
    iadd
    putfield bb/bb_brk/matched_len I
    goto brk_loop
brk_found:
    aload_0
    getfield bb/bb_brk/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_2
    aload_0
    getfield bb/bb_brk/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_brk/matched_len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_2
    aload_0
    getfield bb/bb_brk/matched_len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
brk_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_brk/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_brk/matched_len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── breakx ─────
.class public bb/bb_breakx
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.field private matched_len I
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_breakx/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 3
    aload_0
    iconst_0
    putfield bb/bb_breakx/matched_len I
bx_loop:
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_breakx/matched_len I
    iadd
    istore_1
    iload_1
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge bx_omega
    aload_0
    getfield bb/bb_breakx/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    iload_1
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    ifge bx_check
    aload_0
    dup
    getfield bb/bb_breakx/matched_len I
    iconst_1
    iadd
    putfield bb/bb_breakx/matched_len I
    goto bx_loop
bx_check:
    ; BREAKX: fail if zero advance
    aload_0
    getfield bb/bb_breakx/matched_len I
    ifeq bx_omega
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_2
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_breakx/matched_len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_2
    aload_0
    getfield bb/bb_breakx/matched_len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
bx_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_breakx/matched_len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── len ─────
.class public bb/bb_len
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_len/n I
    aload_0
    aconst_null
    putfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method public <init>(Lbb/bb_box$MatchState;Ljava/util/function/IntSupplier;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iconst_0
    putfield bb/bb_len/n I
    aload_0
    aload_2
    putfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    ifnull len_val_static
    aload_0
    getfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
len_val_static:
    aload_0
    invokevirtual bb/bb_len/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_len/val()I
    iadd
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpgt len_omega
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_len/val()I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    aload_0
    invokevirtual bb/bb_len/val()I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
len_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_len/val()I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── pos ─────
.class public bb/bb_pos
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_pos/n I
    aload_0
    aconst_null
    putfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method public <init>(Lbb/bb_box$MatchState;Ljava/util/function/IntSupplier;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iconst_0
    putfield bb/bb_pos/n I
    aload_0
    aload_2
    putfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    ifnull pos_val_static
    aload_0
    getfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
pos_val_static:
    aload_0
    invokevirtual bb/bb_pos/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    aload_0
    getfield bb/bb_pos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_pos/val()I
    if_icmpne pos_omega
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_pos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
pos_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── tab ─────
.class public bb/bb_tab
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.field private advance I
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_tab/n I
    aload_0
    aconst_null
    putfield bb/bb_tab/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method public <init>(Lbb/bb_box$MatchState;Ljava/util/function/IntSupplier;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iconst_0
    putfield bb/bb_tab/n I
    aload_0
    aload_2
    putfield bb/bb_tab/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_tab/dyn Ljava/util/function/IntSupplier;
    ifnull tab_val_static
    aload_0
    getfield bb/bb_tab/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
tab_val_static:
    aload_0
    invokevirtual bb/bb_tab/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 3
    aload_0
    getfield bb/bb_tab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_tab/val()I
    if_icmpgt tab_omega
    aload_0
    invokevirtual bb/bb_tab/val()I
    aload_0
    getfield bb/bb_tab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    isub
    istore_1
    aload_0
    iload_1
    putfield bb/bb_tab/advance I
    aload_0
    getfield bb/bb_tab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_2
    aload_0
    getfield bb/bb_tab/ms Lbb/bb_box$MatchState;
    aload_0
    invokevirtual bb/bb_tab/val()I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_2
    iload_1
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
tab_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_tab/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_tab/advance I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── rem ─────
.class public bb/bb_rem
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_rem/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_rem/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/omega I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    aload_0
    getfield bb/bb_rem/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iload_1
    isub
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── eps ─────
.class public bb/bb_eps
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private done Z
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    aload_0
    getfield bb/bb_eps/done Z
    ifne eps_omega
    aload_0
    iconst_1
    putfield bb/bb_eps/done Z
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_eps/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
eps_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── bal ─────
.class public bb/bb_bal
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private bal_len I
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 4
    ; depth=0; len=0
    iconst_0
    istore_1               ; depth
    aload_0
    iconst_0
    putfield bb/bb_bal/bal_len I
bal_loop:
    ; while delta+len < omega
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    iadd
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge bal_omega
    ; ch = sigma.charAt(delta+len)
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    iadd
    invokevirtual java/lang/String/charAt(I)C
    istore_2               ; ch
    ; if ch == '(' → depth++; len++
    iload_2
    bipush 40              ; '('
    if_icmpne bal_not_open
    iinc 1 1
    aload_0
    dup
    getfield bb/bb_bal/bal_len I
    iconst_1
    iadd
    putfield bb/bb_bal/bal_len I
    goto bal_check
bal_not_open:
    ; if ch == ')' && depth > 0 → depth--; len++
    iload_2
    bipush 41              ; ')'
    if_icmpne bal_other
    iload_1
    ifle bal_omega         ; depth == 0 and ')' → unmatched, stop
    iinc 1 -1
    aload_0
    dup
    getfield bb/bb_bal/bal_len I
    iconst_1
    iadd
    putfield bb/bb_bal/bal_len I
    goto bal_check
bal_other:
    ; other char: len++
    aload_0
    dup
    getfield bb/bb_bal/bal_len I
    iconst_1
    iadd
    putfield bb/bb_bal/bal_len I
bal_check:
    ; if depth == 0 && len > 0 → success
    iload_1
    ifne bal_loop
    aload_0
    getfield bb/bb_bal/bal_len I
    ifle bal_loop
    ; success
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_3
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_3
    aload_0
    getfield bb/bb_bal/bal_len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
bal_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── abort ─────
.class public bb/bb_abort
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.inner class public static final abort_exception inner bb/bb_abort$AbortException outer bb/bb_abort

.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 1
    new bb/bb_abort$AbortException
    dup
    invokespecial bb/bb_abort$AbortException/<init>()V
    athrow
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 1
    new bb/bb_abort$AbortException
    dup
    invokespecial bb/bb_abort$AbortException/<init>()V
    athrow
.end method

; ───── not ─────
.class public bb/bb_not
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final child Lbb/bb_box;
.field private start I

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_not/child Lbb/bb_box;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    aload_0
    getfield bb/bb_not/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_not/start I
    aload_0
    getfield bb/bb_not/child Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    ; if child succeeded → NOT_ω
    aload_1
    ifnonnull not_omega
    ; child failed → restore delta, return zero-width
    aload_0
    getfield bb/bb_not/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_not/start I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_not/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
not_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── interr ─────
.class public bb/bb_interr
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final child Lbb/bb_box;
.field private start I

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_interr/child Lbb/bb_box;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    aload_0
    getfield bb/bb_interr/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_interr/start I
    aload_0
    getfield bb/bb_interr/child Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull interr_omega
    ; child succeeded: restore delta, return zero-width
    aload_0
    getfield bb/bb_interr/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_interr/start I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_interr/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
interr_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── capture ─────
.class public bb/bb_capture
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.inner interface public static abstract var_setter inner bb/bb_capture$VarSetter outer bb/bb_capture
.field private final child Lbb/bb_box;
.field private final varname Ljava/lang/String;
.field private final immediate Z
.field private final setter Lbb/bb_capture$VarSetter;
.field private pending_start I
.field private pending_len I
.field private has_pending Z

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;Ljava/lang/String;ZLbb/bb_capture$VarSetter;)V
    .limit stack 3
    .limit locals 6
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_capture/child Lbb/bb_box;
    aload_0
    aload_3
    putfield bb/bb_capture/varname Ljava/lang/String;
    aload_0
    iload 4
    putfield bb/bb_capture/immediate Z
    aload_0
    aload 5
    putfield bb/bb_capture/setter Lbb/bb_capture$VarSetter;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 3
    .limit locals 2
    aload_0
    getfield bb/bb_capture/child Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_0
    aload_1
    invokevirtual bb/bb_capture/runChild(Lbb/bb_box$Spec;)Lbb/bb_box$Spec;
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 3
    .limit locals 2
    aload_0
    getfield bb/bb_capture/child Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    astore_1
    aload_0
    aload_1
    invokevirtual bb/bb_capture/runChild(Lbb/bb_box$Spec;)Lbb/bb_box$Spec;
    areturn
.end method

; private Spec runChild(Spec cr)
.method private runChild(Lbb/bb_box$Spec;)Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 3
    aload_1
    ifnonnull cap_got_match
    ; CAP_ω
    aload_0
    iconst_0
    putfield bb/bb_capture/has_pending Z
    aconst_null
    areturn
cap_got_match:
    ; matched = sigma.substring(cr.start, cr.start + cr.len)
    aload_0
    getfield bb/bb_capture/varname Ljava/lang/String;
    ifnull cap_skip_assign
    aload_0
    getfield bb/bb_capture/varname Ljava/lang/String;
    invokevirtual java/lang/String/isEmpty()Z
    ifne cap_skip_assign
    aload_0
    getfield bb/bb_capture/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_1
    getfield bb/bb_box$Spec/start I
    aload_1
    getfield bb/bb_box$Spec/start I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    invokevirtual java/lang/String/substring(II)Ljava/lang/String;
    astore_2                           ; matched string
    aload_0
    getfield bb/bb_capture/immediate Z
    ifeq cap_deferred
    ; immediate ($var): setter.set(varname, matched)
    aload_0
    getfield bb/bb_capture/setter Lbb/bb_capture$VarSetter;
    aload_0
    getfield bb/bb_capture/varname Ljava/lang/String;
    aload_2
    invokeinterface bb/bb_capture$VarSetter/set(Ljava/lang/String;Ljava/lang/String;)V 3
    goto cap_skip_assign
cap_deferred:
    ; deferred (.var): buffer pending
    aload_0
    aload_1
    getfield bb/bb_box$Spec/start I
    putfield bb/bb_capture/pending_start I
    aload_0
    aload_1
    getfield bb/bb_box$Spec/len I
    putfield bb/bb_capture/pending_len I
    aload_0
    iconst_1
    putfield bb/bb_capture/has_pending Z
cap_skip_assign:
    aload_1
    areturn
.end method

; public void commitPending()
.method public commitPending()V
    .limit stack 8
    .limit locals 2
    aload_0
    getfield bb/bb_capture/has_pending Z
    ifeq commit_done
    aload_0
    getfield bb/bb_capture/varname Ljava/lang/String;
    ifnull commit_done
    aload_0
    getfield bb/bb_capture/varname Ljava/lang/String;
    invokevirtual java/lang/String/isEmpty()Z
    ifne commit_done
    ; setter.set(varname, sigma.substring(pending_start, pending_start+pending_len))
    aload_0
    getfield bb/bb_capture/setter Lbb/bb_capture$VarSetter;
    aload_0
    getfield bb/bb_capture/varname Ljava/lang/String;
    aload_0
    getfield bb/bb_capture/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_capture/pending_start I
    aload_0
    getfield bb/bb_capture/pending_start I
    aload_0
    getfield bb/bb_capture/pending_len I
    iadd
    invokevirtual java/lang/String/substring(II)Ljava/lang/String;
    invokeinterface bb/bb_capture$VarSetter/set(Ljava/lang/String;Ljava/lang/String;)V 3
    aload_0
    iconst_0
    putfield bb/bb_capture/has_pending Z
commit_done:
    return
.end method

; ───── atp ─────
.class public bb/bb_atp
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final varname Ljava/lang/String;
.field private final setter Lbb/bb_atp$IntSetter;

.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;Lbb/bb_atp$IntSetter;)V
    .limit stack 3
    .limit locals 4
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_atp/varname Ljava/lang/String;
    aload_0
    aload_3
    putfield bb/bb_atp/setter Lbb/bb_atp$IntSetter;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    ; if varname != null && !varname.isEmpty() → setter.set(varname, delta)
    aload_0
    getfield bb/bb_atp/varname Ljava/lang/String;
    ifnull atp_skip_set
    aload_0
    getfield bb/bb_atp/varname Ljava/lang/String;
    invokevirtual java/lang/String/isEmpty()Z
    ifne atp_skip_set
    aload_0
    getfield bb/bb_atp/setter Lbb/bb_atp$IntSetter;
    aload_0
    getfield bb/bb_atp/varname Ljava/lang/String;
    aload_0
    getfield bb/bb_atp/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    invokeinterface bb/bb_atp$IntSetter/set(Ljava/lang/String;I)V 3
atp_skip_set:
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_atp/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── dvar ─────
.class public bb/bb_dvar
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.inner interface public static abstract box_resolver inner bb/bb_dvar$BoxResolver outer bb/bb_dvar
.field private final varname Ljava/lang/String;
.field private final resolver Lbb/bb_dvar$BoxResolver;
.field private child Lbb/bb_box;

.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;Lbb/bb_dvar$BoxResolver;)V
    .limit stack 3
    .limit locals 4
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_dvar/varname Ljava/lang/String;
    aload_0
    aload_3
    putfield bb/bb_dvar/resolver Lbb/bb_dvar$BoxResolver;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    ; child = resolver.resolve(varname, ms)
    aload_0
    aload_0
    getfield bb/bb_dvar/resolver Lbb/bb_dvar$BoxResolver;
    aload_0
    getfield bb/bb_dvar/varname Ljava/lang/String;
    aload_0
    getfield bb/bb_dvar/ms Lbb/bb_box$MatchState;
    invokeinterface bb/bb_dvar$BoxResolver/resolve(Ljava/lang/String;Lbb/bb_box$MatchState;)Lbb/bb_box; 3
    putfield bb/bb_dvar/child Lbb/bb_box;
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    ifnonnull dvar_alpha_call
    aconst_null
    areturn
dvar_alpha_call:
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    ifnonnull dvar_beta_call
    aconst_null
    areturn
dvar_beta_call:
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    areturn
.end method

; ───── fence ─────
.class public bb/bb_fence
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_fence/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── fail ─────
.class public bb/bb_fail
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── rpos ─────
.class public bb/bb_rpos
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_rpos/n I
    aload_0
    aconst_null
    putfield bb/bb_rpos/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method public <init>(Lbb/bb_box$MatchState;Ljava/util/function/IntSupplier;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iconst_0
    putfield bb/bb_rpos/n I
    aload_0
    aload_2
    putfield bb/bb_rpos/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_rpos/dyn Ljava/util/function/IntSupplier;
    ifnull rpos_val_static
    aload_0
    getfield bb/bb_rpos/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
rpos_val_static:
    aload_0
    invokevirtual bb/bb_rpos/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    aload_0
    getfield bb/bb_rpos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_rpos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    aload_0
    invokevirtual bb/bb_rpos/val()I
    isub
    if_icmpne rpos_omega
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_rpos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
rpos_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method

; ───── rtab ─────
.class public bb/bb_rtab
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.field private advance I
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_rtab/n I
    aload_0
    aconst_null
    putfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method public <init>(Lbb/bb_box$MatchState;Ljava/util/function/IntSupplier;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iconst_0
    putfield bb/bb_rtab/n I
    aload_0
    aload_2
    putfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    ifnull rtab_val_static
    aload_0
    getfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
rtab_val_static:
    aload_0
    invokevirtual bb/bb_rtab/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 4
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    aload_0
    invokevirtual bb/bb_rtab/val()I
    isub
    istore_1
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iload_1
    if_icmpgt rtab_omega
    iload_1
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    isub
    istore_2
    aload_0
    iload_2
    putfield bb/bb_rtab/advance I
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_3
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    iload_1
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_3
    iload_2
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
rtab_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_rtab/advance I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method

; ───── succeed ─────
.class public bb/bb_succeed
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_succeed/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_succeed/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method

