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
