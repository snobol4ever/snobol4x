.class public bb/bb_executor
.super java/lang/Object
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.inner interface public static abstract var_store inner bb/bb_executor$VarStore outer bb/bb_executor
.inner class public static final abort_exception inner bb/bb_abort$AbortException outer bb/bb_abort
.field private final vars Lbb/bb_executor$VarStore;
.field private final pendingCaptures Ljava/util/ArrayList;

.method public <init>(Lbb/bb_executor$VarStore;)V
    .limit stack 4
    .limit locals 2
    aload_0
    invokespecial java/lang/Object/<init>()V
    aload_0
    aload_1
    putfield bb/bb_executor/vars Lbb/bb_executor$VarStore;
    aload_0
    new java/util/ArrayList
    dup
    invokespecial java/util/ArrayList/<init>()V
    putfield bb/bb_executor/pendingCaptures Ljava/util/ArrayList;
    return
.end method

.method public registerCapture(Lbb/bb_capture;)V
    .limit stack 3
    .limit locals 2
    aload_0
    getfield bb/bb_executor/pendingCaptures Ljava/util/ArrayList;
    aload_1
    invokevirtual java/util/ArrayList/add(Ljava/lang/Object;)Z
    pop
    return
.end method

.method public clearCaptures()V
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_executor/pendingCaptures Ljava/util/ArrayList;
    invokevirtual java/util/ArrayList/clear()V
    return
.end method

; public boolean exec(String subjVar, String subjVal, bb_box root,
;                     boolean hasRepl, String replVal, boolean anchor)
.method public exec(Ljava/lang/String;Ljava/lang/String;Lbb/bb_box;ZLjava/lang/String;Z)Z
    .limit stack 6
    .limit locals 11
    ; local layout:
    ;  0=this 1=subjVar 2=subjVal 3=root 4=hasRepl 5=replVal 6=anchor
    ;  7=ms  8=scanLimit  9=scanPos

    ; ms = new MatchState(subjVal)
    new bb/bb_box$MatchState
    dup
    aload_2
    invokespecial bb/bb_box$MatchState/<init>(Ljava/lang/String;)V
    astore 7

    ; if root == null: assignment only, return true
    aload_3
    ifnonnull exec_has_pattern
    iload 4
    ifeq exec_no_repl_nopattern
    aload_1
    ifnull exec_no_repl_nopattern
    aload_0
    getfield bb/bb_executor/vars Lbb/bb_executor$VarStore;
    aload_1
    aload 5
    invokeinterface bb/bb_executor$VarStore/set(Ljava/lang/String;Ljava/lang/String;)V 3
exec_no_repl_nopattern:
    iconst_1
    ireturn

exec_has_pattern:
    ; scanLimit = anchor ? 0 : ms.omega
    iload 6
    ifeq exec_anchor_omega             ; anchor==false → scan full string
    iconst_0                           ; anchor==true  → only position 0
    goto exec_scan_limit_done
exec_anchor_omega:
    aload 7
    getfield bb/bb_box$MatchState/omega I
exec_scan_limit_done:
    istore 8                           ; scanLimit

    ; for scanPos=0; scanPos<=scanLimit; scanPos++
    iconst_0
    istore 9
exec_scan_loop:
    iload 9
    iload 8
    if_icmpgt exec_scan_exhausted

    ; ms.delta = scanPos
    aload 7
    iload 9
    putfield bb/bb_box$MatchState/delta I

    ; try { result = root.α() }
    aload_3
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore 10                          ; result (null or Spec)
    aload 10
    ifnull exec_scan_next

    ; match succeeded — result in slot 10, discard it (we only need delta from ms)

    ; Phase 5: commit deferred captures
    aload_0
    invokevirtual bb/bb_executor/commitAll()V

    ; Phase 5: replacement
    iload 4
    ifeq exec_match_done
    aload_1
    ifnull exec_match_done
    ; pre = subjVal.substring(0, matchStart=scanPos)  post = subjVal.substring(ms.delta)
    ; Note: ms is in slot 7 — but we re-used it! Fix: keep ms in separate slot.
    ; Actually slot 7 = ms (MatchState object), we only stored result on stack
    ; The pop above discarded the Spec — ms is still safe in slot 7.
    ; But wait: astore 7 would overwrite ms. We didn't do astore 7. The pop discarded Spec. OK.
    aload_0
    getfield bb/bb_executor/vars Lbb/bb_executor$VarStore;
    aload_1
    ; pre + replVal + post
    new java/lang/StringBuilder
    dup
    invokespecial java/lang/StringBuilder/<init>()V
    aload_2
    iconst_0
    iload 9                            ; scanPos = matchStart
    invokevirtual java/lang/String/substring(II)Ljava/lang/String;
    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    aload 5
    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    aload_2
    aload 7
    getfield bb/bb_box$MatchState/delta I  ; cursor after match
    invokevirtual java/lang/String/substring(I)Ljava/lang/String;
    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;
    invokeinterface bb/bb_executor$VarStore/set(Ljava/lang/String;Ljava/lang/String;)V 3
exec_match_done:
    aload_0
    invokevirtual bb/bb_executor/clearCaptures()V
    iconst_1
    ireturn

exec_scan_next:
    ; if anchor: break
    iload 6
    ifne exec_scan_exhausted
    iinc 9 1
    goto exec_scan_loop

exec_scan_exhausted:
    aload_0
    invokevirtual bb/bb_executor/clearCaptures()V
    iconst_0
    ireturn
.end method

; public boolean exec(String subjVar, String subjVal, bb_box$MatchState ms,
;                     bb_box root, boolean hasRepl, String replVal, boolean anchor)
; Canonical 7-arg entry: boxes were built with this same ms object.
; exec updates ms.delta each scan step so all boxes see the advancing cursor.
.method public exec(Ljava/lang/String;Ljava/lang/String;Lbb/bb_box$MatchState;Lbb/bb_box;ZLjava/lang/String;Z)Z
    .limit stack 6
    .limit locals 12
    ; 0=this 1=subjVar 2=subjVal 3=ms 4=root 5=hasRepl 6=replVal 7=anchor
    ; 8=scanLimit  9=scanPos  10=result

    ; Sync ms.sigma/omega with current subjVal; reset delta=0
    aload_3
    aload_2
    putfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_3
    aload_2
    invokevirtual java/lang/String/length()I
    putfield bb/bb_box$MatchState/omega I
    aload_3
    iconst_0
    putfield bb/bb_box$MatchState/delta I

    ; if root == null: assignment only, return true
    aload 4
    ifnonnull exec7_has_pattern
    iload 5
    ifeq exec7_no_repl
    aload_1
    ifnull exec7_no_repl
    aload_0
    getfield bb/bb_executor/vars Lbb/bb_executor$VarStore;
    aload_1
    aload 6
    invokeinterface bb/bb_executor$VarStore/set(Ljava/lang/String;Ljava/lang/String;)V 3
exec7_no_repl:
    iconst_1
    ireturn

exec7_has_pattern:
    ; scanLimit = anchor ? 0 : ms.omega
    iload 7
    ifeq exec7_anchor_omega            ; anchor==false → scan full string
    iconst_0                           ; anchor==true  → only position 0
    goto exec7_scan_limit_done
exec7_anchor_omega:
    aload_3
    getfield bb/bb_box$MatchState/omega I
exec7_scan_limit_done:
    istore 8

    iconst_0
    istore 9
exec7_scan_loop:
    iload 9
    iload 8
    if_icmpgt exec7_exhausted

    ; ms.delta = scanPos  — all boxes share this ms, so cursor propagates
    aload_3
    iload 9
    putfield bb/bb_box$MatchState/delta I

    aload 4
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore 10
    aload 10
    ifnull exec7_next

    ; match succeeded — commit captures
    aload_0
    invokevirtual bb/bb_executor/commitAll()V

    iload 5
    ifeq exec7_match_done
    aload_1
    ifnull exec7_match_done
    aload_0
    getfield bb/bb_executor/vars Lbb/bb_executor$VarStore;
    aload_1
    new java/lang/StringBuilder
    dup
    invokespecial java/lang/StringBuilder/<init>()V
    aload_2
    iconst_0
    iload 9
    invokevirtual java/lang/String/substring(II)Ljava/lang/String;
    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    aload 6
    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    aload_2
    aload_3
    getfield bb/bb_box$MatchState/delta I
    invokevirtual java/lang/String/substring(I)Ljava/lang/String;
    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;
    invokeinterface bb/bb_executor$VarStore/set(Ljava/lang/String;Ljava/lang/String;)V 3
exec7_match_done:
    aload_0
    invokevirtual bb/bb_executor/clearCaptures()V
    iconst_1
    ireturn

exec7_next:
    iload 7
    ifne exec7_exhausted
    iinc 9 1
    goto exec7_scan_loop

exec7_exhausted:
    aload_0
    invokevirtual bb/bb_executor/clearCaptures()V
    iconst_0
    ireturn
.end method

; private void commitAll()
.method private commitAll()V
    .limit stack 4
    .limit locals 3
    iconst_0
    istore_1
commit_loop:
    iload_1
    aload_0
    getfield bb/bb_executor/pendingCaptures Ljava/util/ArrayList;
    invokevirtual java/util/ArrayList/size()I
    if_icmpge commit_done
    aload_0
    getfield bb/bb_executor/pendingCaptures Ljava/util/ArrayList;
    iload_1
    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;
    checkcast bb/bb_capture
    invokevirtual bb/bb_capture/commitPending()V
    iinc 1 1
    goto commit_loop
commit_done:
    return
.end method
