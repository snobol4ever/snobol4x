; bb_arbno.s  _XARBN     zero-or-more greedy; zero-advance guard; β unwinds stack
; spec_t  bb_arbno(void *zeta, int entry)
;   rdi = zeta (arbno_t*)  esi = entry
; arbno_t: { bb_box_fn fn @0; void *state @8; int depth @16; [pad4];
;             arbno_frame_t stack[64] @24 }
; arbno_frame_t: { spec_t matched @0{σ@0,δ@8}; int start @16 }  sizeof=24
; stack[i] offset = 24 + i*24

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
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
