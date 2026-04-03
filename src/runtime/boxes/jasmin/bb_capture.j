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
