; bb_not.s   _XNOT       \X — succeed iff X fails; β always ω (no retry)
; spec_t  bb_not(void *zeta, int entry)
;   rdi = zeta (not_t*)    esi = entry (0=α, 1=β)
; not_t: { bb_box_fn fn @0; void *state @8; int start @16 }
; spec_t return: rax=σ, rdx=δ   |   spec_empty: rax=0, rdx=0
;
; Semantics (o$nta/b/c):
;   α: save Δ; call child(α); if child γ → NOT_ω; else restore Δ → NOT_γ(0)
;   β: NOT_ω unconditionally — negation has no retry

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
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
