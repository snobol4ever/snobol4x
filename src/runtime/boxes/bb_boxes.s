; bb_boxes.s — All Byrd box x86-64 implementations, consolidated
; AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
; ───── lit ─────
; bb_lit.s   _XCHR       literal string match

global bb_lit

bb_lit:                                                                 ; rdi=zeta, esi=entry
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (lit_t*)
        ; spec_t LIT (stored on stack as σ:ptr @[rsp+0], δ:int @[rsp+8])
        sub     rsp, 16
        cmp     esi, 0
        je      LIT_α
        jmp     LIT_β
LIT_α:  mov     eax, dword [rel Δ]      ; Δ
        mov     ecx, dword [rbx+8]      ; ζ->len
        add     eax, ecx                ; Δ + ζ->len
        cmp     eax, dword [rel Ω]      ; > Ω ?
        jg      LIT_ω
        ; memcmp(Σ+Δ, ζ->lit, ζ->len)
        mov     r12, qword [rel Σ]      ; r12 = Σ
        movsxd  rax, dword [rel Δ]      ; Δ (sign-extended)
        lea     rdi, [r12+rax]          ; Σ+Δ
        mov     rsi, qword [rbx+0]      ; ζ->lit
        movsxd  rdx, dword [rbx+8]      ; ζ->len
        call    memcmp
        test    eax, eax
        jne     LIT_ω
        ; LIT = spec(Σ+Δ, ζ->len); Δ += ζ->len
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        mov     ecx, dword [rbx+8]      ; δ = ζ->len
        mov     qword [rsp+0], rax      ; LIT.σ
        mov     dword [rsp+8], ecx      ; LIT.δ
        add     dword [rel Δ], ecx      ; Δ += ζ->len
        jmp     LIT_γ
LIT_β:  mov     ecx, dword [rbx+8]      ; ζ->len
        sub     dword [rel Δ], ecx      ; Δ -= ζ->len
        jmp     LIT_ω
LIT_γ:  mov     rax, qword [rsp+0]      ; return LIT.σ
        movsxd  rdx, dword [rsp+8]      ; return LIT.δ
        add     rsp, 16
        pop     r12
        pop     rbx
        ret
LIT_ω:  xor     eax, eax                ; return spec_empty
        xor     edx, edx
        add     rsp, 16
        pop     r12
        pop     rbx
        ret


; ───── seq ─────
; bb_seq.s   _XCAT       concatenation: left then right; β retries right then left
; seq_t: { bb_child_t left @0{fn@0,state@8}; bb_child_t right @16{fn@16,state@24}; spec_t matched @32{σ@32,δ@40} }
; bb_child_t: { void *fn @0; void *state @8 }   sizeof=16

global bb_seq

; Callee-saved: rbx=ζ, r12=lr.σ, r13=lr.δ, r14=rr.σ, r15=rr.δ
bb_seq:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8                  ; align
        mov     rbx, rdi                ; rbx = ζ (seq_t*)
        cmp     esi, 0
        je      SEQ_α
        jmp     SEQ_β

SEQ_α:  ; ζ->matched = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx
        mov     qword [rbx+32], rax     ; ζ->matched.σ = Σ+Δ
        mov     dword [rbx+40], 0       ; ζ->matched.δ = 0
        ; lr = ζ->left.fn(ζ->left.state, α)
        mov     rdi, qword [rbx+8]      ; ζ->left.state
        xor     esi, esi                ; α=0
        call    qword [rbx+0]           ; ζ->left.fn(...)
        mov     r12, rax                ; lr.σ
        movsxd  r13, edx                ; lr.δ
        test    r12, r12
        jz      left_ω
        jmp     left_γ

SEQ_β:  ; rr = ζ->right.fn(ζ->right.state, β)
        mov     rdi, qword [rbx+24]     ; ζ->right.state
        mov     esi, 1                  ; β=1
        call    qword [rbx+16]          ; ζ->right.fn(...)
        mov     r14, rax                ; rr.σ
        movsxd  r15, edx                ; rr.δ
        test    r14, r14
        jz      right_ω
        jmp     right_γ

left_γ: ; ζ->matched = spec_cat(ζ->matched, lr)
        ; spec_cat: same σ, sum δ
        mov     eax, dword [rbx+40]
        add     eax, r13d               ; ζ->matched.δ += lr.δ
        mov     dword [rbx+40], eax
        ; rr = ζ->right.fn(ζ->right.state, α)
        mov     rdi, qword [rbx+24]
        xor     esi, esi
        call    qword [rbx+16]
        mov     r14, rax
        movsxd  r15, edx
        test    r14, r14
        jz      right_ω
        jmp     right_γ

left_ω: jmp     SEQ_ω

right_γ:; SEQ = spec_cat(ζ->matched, rr)
        mov     rax, qword [rbx+32]     ; ζ->matched.σ
        mov     ecx, dword [rbx+40]
        add     ecx, r15d               ; ζ->matched.δ + rr.δ
        movsxd  rdx, ecx
        jmp     SEQ_γ

right_ω:; lr = ζ->left.fn(ζ->left.state, β)
        ; first undo the matched.δ accumulation from left_γ
        mov     eax, dword [rbx+40]
        sub     eax, r13d               ; undo lr contribution
        mov     dword [rbx+40], eax
        mov     rdi, qword [rbx+8]
        mov     esi, 1                  ; β
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      left_ω
        jmp     left_γ

SEQ_γ:  add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
SEQ_ω:  xor     eax, eax
        xor     edx, edx
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── alt ─────
; bb_alt.s   _XOR        alternation: try each child on α; β retries same child
; alt_t: { int n @0; [pad4]; bb_altchild_t children[16] @8  (each 16 bytes: fn@+0,state@+8);
;           int current @264; int position @268; spec_t result @272{σ@272,δ@280} }
; sizeof(alt_t) = 288

global bb_alt

; children[i] offset = 8 + i*16
; children[i].fn    = 8  + i*16
; children[i].state = 16 + i*16

bb_alt:
        push    rbx
        push    r12
        push    r13
        sub     rsp, 8                  ; align
        mov     rbx, rdi                ; rbx = ζ (alt_t*)
        cmp     esi, 0
        je      ALT_α
        jmp     ALT_β

ALT_α:  ; ζ->position = Δ;  ζ->current = 1
        mov     eax, dword [rel Δ]
        mov     dword [rbx+268], eax    ; ζ->position = Δ
        mov     dword [rbx+264], 1      ; ζ->current = 1
        ; cr = ζ->children[0].fn(ζ->children[0].state, α)
        mov     rdi, qword [rbx+16]     ; children[0].state
        xor     esi, esi
        call    qword [rbx+8]           ; children[0].fn
        mov     r12, rax                ; cr.σ
        movsxd  r13, edx                ; cr.δ
        test    r12, r12
        jz      child_α_ω
        jmp     child_α_γ

ALT_β:  ; cr = ζ->children[ζ->current-1].fn(..., β)
        mov     eax, dword [rbx+264]    ; ζ->current
        dec     eax                     ; current-1
        ; offset = 8 + (current-1)*16
        imul    eax, 16
        add     eax, 8                  ; base offset of children[current-1]
        movsxd  rcx, eax
        mov     rdi, qword [rbx+rcx+8]  ; .state
        mov     esi, 1
        call    qword [rbx+rcx]         ; .fn
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      ALT_ω
        jmp     child_β_γ

child_α_γ:
        ; ζ->result = cr
        mov     qword [rbx+272], r12    ; result.σ
        mov     dword [rbx+280], r13d   ; result.δ
        jmp     ALT_γ

child_α_ω:
        ; ζ->current++
        add     dword [rbx+264], 1
        ; if (ζ->current > ζ->n) goto ω
        mov     eax, dword [rbx+264]
        cmp     eax, dword [rbx+0]      ; ζ->n
        jg      ALT_ω
        ; Δ = ζ->position
        mov     eax, dword [rbx+268]
        mov     dword [rel Δ], eax
        ; cr = ζ->children[ζ->current-1].fn(..., α)
        mov     eax, dword [rbx+264]
        dec     eax
        imul    eax, 16
        add     eax, 8
        movsxd  rcx, eax
        mov     rdi, qword [rbx+rcx+8]
        xor     esi, esi
        call    qword [rbx+rcx]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      child_α_ω
        jmp     child_α_γ

child_β_γ:
        mov     qword [rbx+272], r12
        mov     dword [rbx+280], r13d
        jmp     ALT_γ

ALT_γ:  mov     rax, qword [rbx+272]    ; return ζ->result
        movsxd  rdx, dword [rbx+280]
        add     rsp, 8
        pop     r13
        pop     r12
        pop     rbx
        ret
ALT_ω:  xor     eax, eax
        xor     edx, edx
        add     rsp, 8
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── arb ─────
; bb_arb.s   _XFARB      match 0..n chars lazily; β extends by 1
; arb_t: { int count @0; int start @4 }

global bb_arb

bb_arb:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (arb_t*)
        cmp     esi, 0
        je      ARB_α
        jmp     ARB_β
ARB_α:  ; ζ->count=0; ζ->start=Δ; ARB=spec(Σ+Δ,0)
        mov     dword [rbx+0], 0        ; ζ->count = 0
        mov     eax, dword [rel Δ]
        mov     dword [rbx+4], eax      ; ζ->start = Δ
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     ARB_γ
ARB_β:  ; ζ->count++
        add     dword [rbx+0], 1
        ; if (ζ->start + ζ->count > Ω) goto ω
        mov     eax, dword [rbx+4]      ; ζ->start
        add     eax, dword [rbx+0]      ; + ζ->count
        cmp     eax, dword [rel Ω]
        jg      ARB_ω
        ; Δ = ζ->start;  ARB = spec(Σ+Δ, ζ->count);  Δ += ζ->count
        mov     eax, dword [rbx+4]
        mov     dword [rel Δ], eax      ; Δ = ζ->start
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rbx+4]
        add     rax, rcx                ; σ = Σ+ζ->start
        mov     edx, dword [rbx+0]      ; δ = ζ->count
        add     dword [rel Δ], edx      ; Δ += ζ->count
        jmp     ARB_γ
ARB_γ:  pop     rbx
        ret
ARB_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── arbno ─────
; bb_arbno.s  _XARBN     zero-or-more greedy; zero-advance guard; β unwinds stack
; arbno_t: { bb_box_fn fn @0; void *state @8; int depth @16; [pad4];
;             arbno_frame_t stack[64] @24 }
; arbno_frame_t: { spec_t matched @0{σ@0,δ@8}; int start @16 }  sizeof=24
; stack[i] offset = 24 + i*24

global bb_arbno

; Helper: ζ->stack[ζ->depth] address → rcx
; destroys eax
%macro FRAME_PTR 0
        mov     eax, dword [rbx+16]     ; ζ->depth
        imul    eax, 24
        add     eax, 24                 ; 24 + depth*24
        movsxd  rcx, eax
%endmacro

bb_arbno:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8
        mov     rbx, rdi                ; rbx = ζ
        cmp     esi, 0
        je      ARBNO_α
        jmp     ARBNO_β

ARBNO_α:
        ; ζ->depth = 0
        mov     dword [rbx+16], 0
        ; fr = &ζ->stack[0]
        ; fr->matched = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx
        mov     qword [rbx+24], rax     ; stack[0].matched.σ = Σ+Δ
        mov     dword [rbx+32], 0       ; stack[0].matched.δ = 0
        ; fr->start = Δ
        mov     eax, dword [rel Δ]
        mov     dword [rbx+40], eax     ; stack[0].start = Δ
        jmp     ARBNO_try

ARBNO_try:
        ; br = ζ->fn(ζ->state, α)
        mov     rdi, qword [rbx+8]      ; ζ->state
        xor     esi, esi
        call    qword [rbx+0]           ; ζ->fn
        mov     r12, rax                ; br.σ
        movsxd  r13, edx                ; br.δ
        test    r12, r12
        jz      body_ω
        jmp     body_γ

ARBNO_β:
        ; if (ζ->depth <= 0) goto ω
        cmp     dword [rbx+16], 0
        jle     ARBNO_ω
        ; ζ->depth--
        sub     dword [rbx+16], 1
        ; fr = &ζ->stack[ζ->depth]; Δ = fr->start
        FRAME_PTR
        mov     eax, dword [rbx+rcx+16] ; fr->start
        mov     dword [rel Δ], eax
        jmp     ARBNO_γ

body_γ: ; fr = &ζ->stack[ζ->depth]
        FRAME_PTR
        ; if (Δ == fr->start) goto ARBNO_γ_now
        mov     eax, dword [rel Δ]
        cmp     eax, dword [rbx+rcx+16]
        je      ARBNO_γ_now
        ; ARBNO = spec_cat(fr->matched, br)
        ;   σ = fr->matched.σ,  δ = fr->matched.δ + br.δ
        mov     r14, qword [rbx+rcx+0]  ; fr->matched.σ
        mov     eax, dword [rbx+rcx+8]  ; fr->matched.δ
        add     eax, r13d               ; + br.δ
        mov     r15d, eax               ; ARBNO.δ
        ; if (ζ->depth+1 < 64) push new frame
        mov     eax, dword [rbx+16]
        inc     eax
        cmp     eax, 64
        jge     .no_push
        mov     dword [rbx+16], eax     ; ζ->depth++
        FRAME_PTR                       ; rcx = new frame offset
        ; fr->matched = ARBNO
        mov     qword [rbx+rcx+0], r14
        mov     dword [rbx+rcx+8], r15d
        ; fr->start = Δ
        mov     eax, dword [rel Δ]
        mov     dword [rbx+rcx+16], eax
.no_push:
        jmp     ARBNO_try

body_ω: ; ARBNO = ζ->stack[ζ->depth].matched
        FRAME_PTR
        mov     rax, qword [rbx+rcx+0]  ; matched.σ
        movsxd  rdx, dword [rbx+rcx+8] ; matched.δ
        jmp     ARBNO_γ

ARBNO_γ_now:
        FRAME_PTR
        mov     rax, qword [rbx+rcx+0]
        movsxd  rdx, dword [rbx+rcx+8]
        jmp     ARBNO_γ

ARBNO_γ:
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
ARBNO_ω:
        xor     eax, eax
        xor     edx, edx
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── any ─────
; bb_any.s   _XANYC      match one char if in set
; any_t: { const char *chars @0 }

global bb_any

bb_any:
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (any_t*)
        cmp     esi, 0
        je      ANY_α
        jmp     ANY_β
ANY_α:  ; if (Δ>=Ω || !strchr(ζ->chars, Σ[Δ])) goto ω
        mov     eax, dword [rel Δ]
        cmp     eax, dword [rel Ω]
        jge     ANY_ω
        ; load Σ[Δ]
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        movzx   esi, byte [r12+rcx]     ; Σ[Δ]
        mov     rdi, qword [rbx+0]      ; ζ->chars
        call    strchr
        test    rax, rax
        jz      ANY_ω
        ; ANY = spec(Σ+Δ, 1);  Δ++
        mov     rax, r12
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, 1                  ; δ = 1
        add     dword [rel Δ], 1        ; Δ++
        jmp     ANY_γ
ANY_β:  sub     dword [rel Δ], 1        ; Δ--
        jmp     ANY_ω
ANY_γ:  pop     r12
        pop     rbx
        ret
ANY_ω:  xor     eax, eax
        xor     edx, edx
        pop     r12
        pop     rbx
        ret

; ───── notany ─────
; bb_notany.s  _XNNYC    match one char if NOT in set
; notany_t: { const char *chars @0 }

global bb_notany

bb_notany:
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (notany_t*)
        cmp     esi, 0
        je      NOTANY_α
        jmp     NOTANY_β
NOTANY_α:
        ; if (Δ>=Ω || strchr(ζ->chars, Σ[Δ])) goto ω
        mov     eax, dword [rel Δ]
        cmp     eax, dword [rel Ω]
        jge     NOTANY_ω
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        movzx   esi, byte [r12+rcx]     ; Σ[Δ]
        mov     rdi, qword [rbx+0]      ; ζ->chars
        call    strchr
        test    rax, rax
        jnz     NOTANY_ω                ; found → NOT in set fails
        ; NOTANY = spec(Σ+Δ, 1);  Δ++
        mov     rax, r12
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, 1                  ; δ = 1
        add     dword [rel Δ], 1        ; Δ++
        jmp     NOTANY_γ
NOTANY_β:
        sub     dword [rel Δ], 1        ; Δ--
        jmp     NOTANY_ω
NOTANY_γ:
        pop     r12
        pop     rbx
        ret
NOTANY_ω:
        xor     eax, eax
        xor     edx, edx
        pop     r12
        pop     rbx
        ret

; ───── span ─────
; bb_span.s  _XSPNC      longest prefix of chars in set (≥1)
; span_t: { const char *chars @0; int δ @8 }

global bb_span

bb_span:
        push    rbx
        push    r12
        push    r13
        mov     rbx, rdi                ; rbx = ζ (span_t*)
        cmp     esi, 0
        je      SPAN_α
        jmp     SPAN_β
SPAN_α: ; ζ->δ = 0
        mov     dword [rbx+8], 0
        ; while (Δ+ζ->δ < Ω && strchr(ζ->chars, Σ[Δ+ζ->δ])) ζ->δ++
        mov     r12, qword [rel Σ]      ; r12 = Σ
        mov     r13, qword [rbx+0]      ; r13 = ζ->chars
.span_loop:
        mov     eax, dword [rel Δ]
        add     eax, dword [rbx+8]      ; Δ+ζ->δ
        cmp     eax, dword [rel Ω]
        jge     .span_done
        movsxd  rcx, eax
        movzx   esi, byte [r12+rcx]     ; Σ[Δ+ζ->δ]
        mov     rdi, r13
        call    strchr
        test    rax, rax
        jz      .span_done
        add     dword [rbx+8], 1        ; ζ->δ++
        jmp     .span_loop
.span_done:
        ; if (ζ->δ <= 0) goto ω
        cmp     dword [rbx+8], 0
        jle     SPAN_ω
        ; SPAN = spec(Σ+Δ, ζ->δ);  Δ += ζ->δ
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        mov     edx, dword [rbx+8]      ; δ = ζ->δ
        add     dword [rel Δ], edx      ; Δ += ζ->δ
        jmp     SPAN_γ
SPAN_β: mov     ecx, dword [rbx+8]      ; ζ->δ
        sub     dword [rel Δ], ecx      ; Δ -= ζ->δ
        jmp     SPAN_ω
SPAN_γ: pop     r13
        pop     r12
        pop     rbx
        ret
SPAN_ω: xor     eax, eax
        xor     edx, edx
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── brk ─────
; bb_brk.s   _XBRKC      scan to first char in set (may be zero-width)
; brk_t: { const char *chars @0; int δ @8 }

global bb_brk

bb_brk:
        push    rbx
        push    r12
        push    r13
        mov     rbx, rdi                ; rbx = ζ (brk_t*)
        cmp     esi, 0
        je      BRK_α
        jmp     BRK_β
BRK_α:  ; ζ->δ = 0
        mov     dword [rbx+8], 0
        ; while (Δ+ζ->δ < Ω && !strchr(ζ->chars, Σ[Δ+ζ->δ])) ζ->δ++
        mov     r12, qword [rel Σ]
        mov     r13, qword [rbx+0]      ; ζ->chars
.brk_loop:
        mov     eax, dword [rel Δ]
        add     eax, dword [rbx+8]      ; Δ+ζ->δ
        cmp     eax, dword [rel Ω]
        jge     .brk_done
        movsxd  rcx, eax
        movzx   esi, byte [r12+rcx]
        mov     rdi, r13
        call    strchr
        test    rax, rax
        jnz     .brk_done               ; found break char → stop
        add     dword [rbx+8], 1        ; ζ->δ++
        jmp     .brk_loop
.brk_done:
        ; if (Δ+ζ->δ >= Ω) goto ω
        mov     eax, dword [rel Δ]
        add     eax, dword [rbx+8]
        cmp     eax, dword [rel Ω]
        jge     BRK_ω
        ; BRK = spec(Σ+Δ, ζ->δ);  Δ += ζ->δ
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        mov     edx, dword [rbx+8]      ; δ = ζ->δ
        add     dword [rel Δ], edx      ; Δ += ζ->δ
        jmp     BRK_γ
BRK_β:  mov     ecx, dword [rbx+8]
        sub     dword [rel Δ], ecx      ; Δ -= ζ->δ
        jmp     BRK_ω
BRK_γ:  pop     r13
        pop     r12
        pop     rbx
        ret
BRK_ω:  xor     eax, eax
        xor     edx, edx
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── breakx ─────
; bb_breakx.s _XBRKX     like BRK but fails on zero advance
; brkx_t: { const char *chars @0; int δ @8 }

global bb_breakx

bb_breakx:
        push    rbx
        push    r12
        push    r13
        mov     rbx, rdi                ; rbx = ζ (brkx_t*)
        cmp     esi, 0
        je      BREAKX_α
        jmp     BREAKX_β
BREAKX_α:
        mov     dword [rbx+8], 0        ; ζ->δ = 0
        mov     r12, qword [rel Σ]
        mov     r13, qword [rbx+0]      ; ζ->chars
.brkx_loop:
        mov     eax, dword [rel Δ]
        add     eax, dword [rbx+8]
        cmp     eax, dword [rel Ω]
        jge     .brkx_done
        movsxd  rcx, eax
        movzx   esi, byte [r12+rcx]
        mov     rdi, r13
        call    strchr
        test    rax, rax
        jnz     .brkx_done
        add     dword [rbx+8], 1
        jmp     .brkx_loop
.brkx_done:
        ; if (ζ->δ==0 || Δ+ζ->δ>=Ω) goto ω
        cmp     dword [rbx+8], 0
        je      BREAKX_ω
        mov     eax, dword [rel Δ]
        add     eax, dword [rbx+8]
        cmp     eax, dword [rel Ω]
        jge     BREAKX_ω
        ; BREAKX = spec(Σ+Δ, ζ->δ);  Δ += ζ->δ
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        mov     edx, dword [rbx+8]      ; δ = ζ->δ
        add     dword [rel Δ], edx
        jmp     BREAKX_γ
BREAKX_β:
        mov     ecx, dword [rbx+8]
        sub     dword [rel Δ], ecx
        jmp     BREAKX_ω
BREAKX_γ:
        pop     r13
        pop     r12
        pop     rbx
        ret
BREAKX_ω:
        xor     eax, eax
        xor     edx, edx
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── len ─────
; bb_len.s   _XLNTH      match exactly n characters
; len_t: { int n @0 }

global bb_len

bb_len:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (len_t*)
        cmp     esi, 0
        je      LEN_α
        jmp     LEN_β
LEN_α:  mov     eax, dword [rel Δ]      ; Δ
        add     eax, dword [rbx+0]      ; Δ + ζ->n
        cmp     eax, dword [rel Ω]      ; > Ω ?
        jg      LEN_ω
        ; LEN = spec(Σ+Δ, ζ->n);  Δ += ζ->n
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, dword [rbx+0]      ; δ = ζ->n
        add     dword [rel Δ], edx      ; Δ += ζ->n
        jmp     LEN_γ
LEN_β:  mov     ecx, dword [rbx+0]      ; ζ->n
        sub     dword [rel Δ], ecx      ; Δ -= ζ->n
        jmp     LEN_ω
LEN_γ:  pop     rbx
        ret
LEN_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── pos ─────
; bb_pos.s   _XPOSI      assert cursor == n (zero-width)
; pos_t: { int n @0 }

global bb_pos

bb_pos:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (pos_t*)
        cmp     esi, 0
        je      POS_α
        jmp     POS_β
POS_α:  mov     eax, dword [rel Δ]      ; Δ
        cmp     eax, dword [rbx+0]      ; Δ != ζ->n ?
        jne     POS_ω
        ; POS = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     POS_γ
POS_β:                                  jmp     POS_ω
POS_γ:  pop     rbx
        ret
POS_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── tab ─────
; bb_tab.s   _XTB        advance cursor TO absolute position n
; tab_t: { int n @0; int advance @4 }

global bb_tab

bb_tab:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (tab_t*)
        cmp     esi, 0
        je      TAB_α
        jmp     TAB_β
TAB_α:  mov     eax, dword [rel Δ]      ; Δ
        cmp     eax, dword [rbx+0]      ; Δ > ζ->n ?
        jg      TAB_ω
        ; ζ->advance = ζ->n - Δ
        mov     ecx, dword [rbx+0]      ; ζ->n
        sub     ecx, dword [rel Δ]      ; ζ->n - Δ
        mov     dword [rbx+4], ecx      ; ζ->advance = ζ->n-Δ
        ; TAB = spec(Σ+Δ, ζ->advance);  Δ = ζ->n
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, dword [rbx+4]      ; δ = ζ->advance
        mov     ecx, dword [rbx+0]
        mov     dword [rel Δ], ecx      ; Δ = ζ->n
        jmp     TAB_γ
TAB_β:  mov     ecx, dword [rbx+4]      ; ζ->advance
        sub     dword [rel Δ], ecx      ; Δ -= ζ->advance
        jmp     TAB_ω
TAB_γ:  pop     rbx
        ret
TAB_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── rem ─────
; bb_rem.s   _XSTAR      match entire remainder; no backtrack
; rem_t: { int dummy @0 }  — state unused

global bb_rem

bb_rem:
        cmp     esi, 0
        je      REM_α
        jmp     REM_β
REM_α:  ; REM = spec(Σ+Δ, Ω-Δ);  Δ = Ω
        mov     rax, qword [rel Σ]      ; rax = Σ
        movsxd  rcx, dword [rel Δ]      ; rcx = old Δ
        add     rax, rcx                ; rax = σ = Σ+Δ
        mov     ecx, dword [rel Ω]
        sub     ecx, dword [rel Δ]      ; ecx = δ = Ω-Δ
        movsxd  rdx, ecx               ; rdx = δ
        ; Δ = Ω
        mov     ecx, dword [rel Ω]
        mov     dword [rel Δ], ecx
        jmp     REM_γ                   ; rax=σ, rdx=δ
REM_β:                                  jmp     REM_ω
REM_γ:                                  ret
REM_ω:  xor     eax, eax
        xor     edx, edx
        ret

; ───── eps ─────
; bb_eps.s   _XEPS       zero-width success once; done flag prevents double-γ
; eps_t: { int done @0 }

global bb_eps

bb_eps:                                                                 ; rdi=zeta, esi=entry
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (eps_t*)
        cmp     esi, 0
        je      .alpha_init
        jmp     EPS_β
.alpha_init:
        mov     dword [rbx+0], 0        ; ζ->done = 0
        jmp     EPS_α
EPS_α:  cmp     dword [rbx+0], 0        ; if (ζ->done)
        jne     EPS_ω
        mov     dword [rbx+0], 1        ; ζ->done = 1
        ; EPS = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     EPS_γ
EPS_β:  jmp     EPS_ω
EPS_γ:                                                                  ; return EPS (rax=σ, rdx=δ)
        pop     rbx
        ret
EPS_ω:  xor     eax, eax                ; return spec_empty
        xor     edx, edx
        pop     rbx
        ret

; ───── bal ─────
; bb_bal.s   _XBAL       balanced parens — STUB; M-DYN-BAL pending
; bal_t: { int δ @0; int start @4 }

.bal_msg: db "bb_bal: unimplemented — ω", 10, 0

global bb_bal

bb_bal:
        push    rbx
        sub     rsp, 8
        ; fprintf(stderr, "bb_bal: unimplemented — ω\n")
        mov     rdi, qword [rel stderr]
        lea     rsi, [rel .bal_msg]
        xor     eax, eax
        call    fprintf
        xor     eax, eax                ; return spec_empty
        xor     edx, edx
        add     rsp, 8
        pop     rbx
        ret

; ───── abort ─────
; bb_abort.s  _XABRT     always ω — force match failure
global bb_abort

bb_abort:
        cmp     esi, 0
        je      ABORT_α
        jmp     ABORT_β
ABORT_α:                                jmp     ABORT_ω
ABORT_β:                                jmp     ABORT_ω
ABORT_ω:
        xor     eax, eax                ; return spec_empty
        xor     edx, edx
        ret

; ───── not ─────
; bb_not.s   _XNOT       \X — succeed iff X fails; β always ω (no retry)
; not_t: { bb_box_fn fn @0; void *state @8; int start @16 }
;
; Semantics (o$nta/b/c):
;   α: save Δ; call child(α); if child γ → NOT_ω; else restore Δ → NOT_γ(0)
;   β: NOT_ω unconditionally — negation has no retry

global bb_not

bb_not:                                                                 ; rdi=zeta, esi=entry
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (not_t*)
        cmp     esi, 0
        je      NOT_α
        jmp     NOT_β
NOT_α:  ; ζ->start = Δ
        mov     eax, dword [rel Δ]
        mov     dword [rbx+16], eax     ; ζ->start = Δ
        ; cr = ζ->fn(ζ->state, α)
        mov     rdi, qword [rbx+8]      ; ζ->state
        xor     esi, esi                ; α=0
        call    qword [rbx+0]           ; ζ->fn(...)
        ; if (!spec_is_empty(cr)) → child succeeded → NOT_ω
        test    rax, rax
        jnz     NOT_ω
        ; child failed → restore Δ; return spec(Σ+Δ, 0)
        mov     eax, dword [rbx+16]
        mov     dword [rel Δ], eax      ; Δ = ζ->start
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     NOT_γ
NOT_β:                                  jmp NOT_ω
NOT_γ:  pop     r12
        pop     rbx
        ret
NOT_ω:  xor     eax, eax                ; return spec_empty
        xor     edx, edx
        pop     r12
        pop     rbx
        ret

; ───── interr ─────
; bb_interr.s _XINT       ?X — null result if X succeeds; ω if X fails (o$int)
; interr_t: { bb_box_fn fn @0; void *state @8; int start @16 }
;
; Semantics (o$int):
;   α: save Δ; call child(α); if child ω → INT_ω;
;      else restore Δ (discard child's match) → INT_γ spec(Σ+Δ,0)
;   β: INT_ω unconditionally — interrogation has no retry

global bb_interr

bb_interr:                                                              ; rdi=zeta, esi=entry
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (interr_t*)
        cmp     esi, 0
        je      INT_α
        jmp     INT_β
INT_α:  ; ζ->start = Δ
        mov     eax, dword [rel Δ]
        mov     dword [rbx+16], eax     ; ζ->start = Δ
        ; cr = ζ->fn(ζ->state, α)
        mov     rdi, qword [rbx+8]      ; ζ->state
        xor     esi, esi                ; α=0
        call    qword [rbx+0]           ; ζ->fn(...)
        ; if spec_is_empty(cr) → child failed → INT_ω
        test    rax, rax
        jz      INT_ω
        ; child succeeded → restore Δ; return spec(Σ+Δ, 0)
        mov     eax, dword [rbx+16]
        mov     dword [rel Δ], eax      ; Δ = ζ->start
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     INT_γ
INT_β:                                  jmp INT_ω
INT_γ:  pop     r12
        pop     rbx
        ret
INT_ω:  xor     eax, eax                ; return spec_empty
        xor     edx, edx
        pop     r12
        pop     rbx
        ret

; ───── capture ─────
; bb_capture.s  _XNME/_XFNME  $ writes on every γ; . buffers for Phase-5 commit
; capture_t: { bb_box_fn fn @0; void *state @8; const char *varname @16;
;              int immediate @24; spec_t pending @32{σ@32,δ@40}; int has_pending @48 }
; sizeof(capture_t) = 56

global bb_capture

bb_capture:
        push    rbx
        push    r12
        push    r13
        sub     rsp, 32                 ; scratch: DESCR_t @[rsp+0..15], align
        mov     rbx, rdi                ; rbx = ζ (capture_t*)
        cmp     esi, 0
        je      CAP_α
        jmp     CAP_β

CAP_α:  ; cr = ζ->fn(ζ->state, α)
        mov     rdi, qword [rbx+8]
        xor     esi, esi
        call    qword [rbx+0]
        mov     r12, rax                ; cr.σ
        movsxd  r13, edx                ; cr.δ
        test    r12, r12
        jz      CAP_ω
        jmp     CAP_γ_core

CAP_β:  ; cr = ζ->fn(ζ->state, β)
        mov     rdi, qword [rbx+8]
        mov     esi, 1
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      CAP_ω
        jmp     CAP_γ_core

CAP_γ_core:
        ; if (ζ->varname && *ζ->varname && ζ->immediate)
        mov     rax, qword [rbx+16]     ; ζ->varname
        test    rax, rax
        jz      .cap_no_imm
        cmp     byte [rax], 0
        je      .cap_no_imm
        cmp     dword [rbx+24], 0       ; ζ->immediate
        je      .cap_cond
        ; immediate: s = GC_MALLOC(cr.δ+1); memcpy(s,cr.σ,cr.δ); s[cr.δ]=0
        movsxd  rdi, r13d
        inc     rdi                     ; cr.δ+1
        call    GC_MALLOC
        mov     qword [rsp+0], rax      ; save s
        mov     rdi, rax
        mov     rsi, r12                ; cr.σ
        movsxd  rdx, r13d
        call    memcpy
        mov     rax, qword [rsp+0]
        movsxd  rcx, r13d
        mov     byte [rax+rcx], 0       ; s[cr.δ] = 0
        ; DESCR_t v = { .v=DT_S=1, .slen=cr.δ, .s=s }
        mov     dword [rsp+16], 1       ; v.v = DT_S
        mov     dword [rsp+20], r13d    ; v.slen = cr.δ
        mov     qword [rsp+24], rax     ; v.s = s
        ; NV_SET_fn(ζ->varname, v)
        mov     rdi, qword [rbx+16]
        mov     rsi, qword [rsp+16]     ; v low qword
        mov     rdx, qword [rsp+24]     ; v high qword
        call    NV_SET_fn
        jmp     .cap_done
.cap_cond:
        ; conditional: ζ->pending = cr; ζ->has_pending = 1
        mov     qword [rbx+32], r12     ; pending.σ = cr.σ
        mov     dword [rbx+40], r13d    ; pending.δ = cr.δ
        mov     dword [rbx+48], 1       ; has_pending = 1
        jmp     .cap_done
.cap_no_imm:
.cap_done:
        ; return cr (r12=σ, r13=δ)
        mov     rax, r12
        movsxd  rdx, r13d
        add     rsp, 32
        pop     r13
        pop     r12
        pop     rbx
        ret

CAP_ω:  ; ζ->has_pending = 0
        mov     dword [rbx+48], 0
        xor     eax, eax
        xor     edx, edx
        add     rsp, 32
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── atp ─────
; bb_atp.s   _XATP       @var — write cursor Δ as DT_I into varname; no backtrack
; atp_t: { int done @0; [pad4]; const char *varname @8 }
; DESCR_t: { DTYPE_t v @0 (int); uint32_t slen @4; union{...} @8 }  — DT_I=6
; NV_SET_fn(const char *name, DESCR_t v)  — v passed as struct (rsi:rdx = v[0..7]:v[8..15])

global bb_atp

bb_atp:
        push    rbx
        push    r12
        sub     rsp, 24                 ; space for DESCR_t (16 bytes) + align
        mov     rbx, rdi                ; rbx = ζ (atp_t*)
        cmp     esi, 0
        je      ATP_α
        jmp     ATP_β
ATP_α:  mov     dword [rbx+0], 1        ; ζ->done = 1
        ; if (ζ->varname && ζ->varname[0])
        mov     r12, qword [rbx+8]      ; ζ->varname
        test    r12, r12
        jz      .atp_no_set
        cmp     byte [r12], 0
        je      .atp_no_set
        ; DESCR_t v = { .v=DT_I=6, .slen=0, .i=(int64_t)Δ }
        ; layout: dword v @0, dword slen @4, qword i @8
        mov     dword [rsp+0], 6        ; v = DT_I
        mov     dword [rsp+4], 0        ; slen = 0
        movsxd  rax, dword [rel Δ]
        mov     qword [rsp+8], rax      ; i = Δ
        ; NV_SET_fn(ζ->varname, v)
        ; SysV: rdi=name, rsi=v[0..7], rdx=v[8..15]
        mov     rdi, r12
        mov     rsi, qword [rsp+0]      ; v low qword (v+slen)
        mov     rdx, qword [rsp+8]      ; v high qword (i)
        call    NV_SET_fn
.atp_no_set:
        ; ATP = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx
        xor     edx, edx
        jmp     ATP_γ
ATP_β:                                  jmp     ATP_ω
ATP_γ:  add     rsp, 24
        pop     r12
        pop     rbx
        ret
ATP_ω:  xor     eax, eax
        xor     edx, edx
        add     rsp, 24
        pop     r12
        pop     rbx
        ret

; ───── dvar ─────
; bb_dvar.s  _XDSAR/_XVAR  *VAR/VAR — re-resolve live value on every α
; dvar_t: { bb_box_fn child_fn @0; void *child_state @8;
;            size_t child_size @16; const char *name @24 }
; NV_GET_fn(const char *name) → DESCR_t (rax=low qword, rdx=high qword)
;   DESCR_t: { DTYPE_t v @0 (int); uint32_t slen @4; union ptr/i @8 }
;   DT_P=3, DT_S=1
; bb_build(void *patnd) → bb_node_t { bb_box_fn fn @0; void *ζ @8; size_t ζ_size @16 }

global bb_deferred_var

; Local frame layout (sub rsp, 48):
;   [rsp+ 0] DESCR_t val  (16 bytes: low @0, high @8)
;   [rsp+16] bb_node_t c  (24 bytes: fn@16, ζ@24, ζ_size@32)  [only 24 used]
;   [rsp+40] pad

bb_deferred_var:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 48
        mov     rbx, rdi                ; rbx = ζ (dvar_t*)
        cmp     esi, 0
        je      DVAR_α
        jmp     DVAR_β

DVAR_α: ; val = NV_GET_fn(ζ->name)
        mov     rdi, qword [rbx+24]     ; ζ->name
        call    NV_GET_fn
        mov     qword [rsp+0], rax      ; val low
        mov     qword [rsp+8], rdx      ; val high
        ; int rebuilt = 0
        xor     r15d, r15d
        ; check DT_P: val.v == 3
        mov     eax, dword [rsp+0]      ; val.v (low dword of low qword)
        cmp     eax, 3                  ; DT_P ?
        jne     .check_dts
        ; val.p (ptr) = high qword
        mov     r12, qword [rsp+8]      ; val.p
        test    r12, r12
        jz      .check_dts
        ; if val.p != ζ->child_state → rebuild
        cmp     r12, qword [rbx+8]
        je      .check_dts
        ; c = bb_build(val.p)
        mov     rdi, r12
        call    bb_build
        ; bb_node_t returned: rax=fn, rdx=ζ ptr ... wait, bb_build returns struct
        ; bb_build returns bb_node_t (24 bytes) — check actual ABI
        ; Since bb_node_t > 16 bytes it uses sret: first arg is hidden ptr
        ; We need to pass &c as hidden first arg
        ; Actually: struct > 16 bytes → hidden pointer in rdi, real arg in rsi
        ; Redo: pass hidden ptr first
        lea     rdi, [rsp+16]           ; &c (hidden sret)
        mov     rsi, r12                ; val.p
        call    bb_build
        mov     r13, qword [rsp+16]     ; c.fn
        mov     r14, qword [rsp+24]     ; c.ζ
        mov     qword [rbx+0], r13      ; ζ->child_fn = c.fn
        mov     qword [rbx+8], r14      ; ζ->child_state = c.ζ
        mov     rax, qword [rsp+32]     ; c.ζ_size
        mov     qword [rbx+16], rax     ; ζ->child_size = c.ζ_size
        mov     r15d, 1                 ; rebuilt = 1
        jmp     .check_reset
.check_dts:
        ; check DT_S: val.v == 1
        mov     eax, dword [rsp+0]
        cmp     eax, 1                  ; DT_S ?
        jne     .check_reset
        mov     r12, qword [rsp+8]      ; val.s (string ptr, high qword)
        test    r12, r12
        jz      .check_reset
        ; if (!lz || lz->lit != val.s) rebuild as lit
        mov     r14, qword [rbx+8]      ; ζ->child_state (lz)
        test    r14, r14
        jz      .rebuild_lit
        ; lz->lit is at offset 0 (lit_t: { const char *lit @0; int len @8 })
        cmp     qword [r14+0], r12      ; lz->lit == val.s?
        je      .check_reset
.rebuild_lit:
        ; lz = calloc(1, 16)  [sizeof(lit_t)=16]
        mov     rdi, 1
        mov     rsi, 16
        call    calloc
        mov     r14, rax                ; lz
        mov     qword [r14+0], r12      ; lz->lit = val.s
        ; lz->len = strlen(val.s) — use slen field from DESCR_t (dword @4 of low qword)
        mov     eax, dword [rsp+4]      ; val.slen
        mov     dword [r14+8], eax      ; lz->len = slen
        mov     qword [rbx+0], bb_lit   ; ζ->child_fn = bb_lit
        mov     qword [rbx+8], r14      ; ζ->child_state = lz
        mov     qword [rbx+16], 16      ; ζ->child_size = sizeof(lit_t)
        mov     r15d, 1                 ; rebuilt = 1
.check_reset:
        ; if (!rebuilt && ζ->child_state && ζ->child_size && ζ->child_fn != bb_lit)
        test    r15d, r15d
        jnz     .call_child
        cmp     qword [rbx+8], 0
        je      .call_child
        cmp     qword [rbx+16], 0
        je      .call_child
        mov     rax, qword [rbx+0]
        lea     rcx, [rel bb_lit]
        cmp     rax, rcx
        je      .call_child
        ; memset(ζ->child_state, 0, ζ->child_size)
        mov     rdi, qword [rbx+8]
        xor     esi, esi
        mov     rdx, qword [rbx+16]
        call    memset
.call_child:
        ; if (!ζ->child_fn) goto ω
        cmp     qword [rbx+0], 0
        je      DVAR_ω
        ; DVAR = ζ->child_fn(ζ->child_state, α)
        mov     rdi, qword [rbx+8]
        xor     esi, esi
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      DVAR_ω
        jmp     DVAR_γ

DVAR_β: ; if (!ζ->child_fn) goto ω
        cmp     qword [rbx+0], 0
        je      DVAR_ω
        ; DVAR = ζ->child_fn(ζ->child_state, β)
        mov     rdi, qword [rbx+8]
        mov     esi, 1
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      DVAR_ω
        jmp     DVAR_γ

DVAR_γ: mov     rax, r12
        movsxd  rdx, r13d
        add     rsp, 48
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
DVAR_ω: xor     eax, eax
        xor     edx, edx
        add     rsp, 48
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; ───── fence ─────
; bb_fence.s  _XFNCE     succeed once; β cuts (no retry)
; fence_t: { int fired @0 }

global bb_fence

bb_fence:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (fence_t*)
        cmp     esi, 0
        je      FENCE_α
        jmp     FENCE_β
FENCE_α:
        mov     dword [rbx+0], 1        ; ζ->fired = 1
        jmp     FENCE_γ
FENCE_β:                                jmp     FENCE_ω
FENCE_γ:
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        pop     rbx
        ret
FENCE_ω:
        xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── fail ─────
; bb_fail.s  _XFAIL      always ω — force backtrack
global bb_fail

bb_fail:
        xor     eax, eax                ; return spec_empty
        xor     edx, edx
        ret

; ───── rpos ─────
; bb_rpos.s  _XRPSI      assert cursor == Ω-n (zero-width)
; rpos_t: { int n @0 }

global bb_rpos

bb_rpos:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (rpos_t*)
        cmp     esi, 0
        je      RPOS_α
        jmp     RPOS_β
RPOS_α: mov     eax, dword [rel Ω]      ; Ω
        sub     eax, dword [rbx+0]      ; Ω-ζ->n
        cmp     dword [rel Δ], eax      ; Δ != Ω-n ?
        jne     RPOS_ω
        ; RPOS = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     RPOS_γ
RPOS_β:                                 jmp     RPOS_ω
RPOS_γ: pop     rbx
        ret
RPOS_ω: xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── rtab ─────
; bb_rtab.s  _XRTB       advance cursor TO position Ω-n
; rtab_t: { int n @0; int advance @4 }

global bb_rtab

bb_rtab:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (rtab_t*)
        cmp     esi, 0
        je      RTAB_α
        jmp     RTAB_β
RTAB_α: ; if (Δ > Ω-ζ->n) goto ω
        mov     eax, dword [rel Ω]
        sub     eax, dword [rbx+0]      ; Ω-ζ->n
        cmp     dword [rel Δ], eax      ; Δ > Ω-n ?
        jg      RTAB_ω
        ; ζ->advance = (Ω-ζ->n) - Δ
        mov     ecx, dword [rel Ω]
        sub     ecx, dword [rbx+0]      ; Ω-ζ->n
        sub     ecx, dword [rel Δ]      ; (Ω-n)-Δ
        mov     dword [rbx+4], ecx      ; ζ->advance
        ; RTAB = spec(Σ+Δ, ζ->advance);  Δ = Ω-ζ->n
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, dword [rbx+4]      ; δ = ζ->advance
        mov     ecx, dword [rel Ω]
        sub     ecx, dword [rbx+0]
        mov     dword [rel Δ], ecx      ; Δ = Ω-ζ->n
        jmp     RTAB_γ
RTAB_β: mov     ecx, dword [rbx+4]      ; ζ->advance
        sub     dword [rel Δ], ecx      ; Δ -= ζ->advance
        jmp     RTAB_ω
RTAB_γ: pop     rbx
        ret
RTAB_ω: xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret

; ───── succeed ─────
; bb_succeed.s  _XSUCF    always γ zero-width; outer loop retries

global bb_succeed

bb_succeed:
        mov     rax, qword [rel Σ]      ; σ = Σ
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        ret                             ; return spec(Σ+Δ, 0)

