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
