; bb_interr.s _XINT       ?X — null result if X succeeds; ω if X fails (o$int)
; spec_t  bb_interr(void *zeta, int entry)
;   rdi = zeta (interr_t*)  esi = entry (0=α, 1=β)
; interr_t: { bb_box_fn fn @0; void *state @8; int start @16 }
; spec_t return: rax=σ, rdx=δ   |   spec_empty: rax=0, rdx=0
;
; Semantics (o$int):
;   α: save Δ; call child(α); if child ω → INT_ω;
;      else restore Δ (discard child's match) → INT_γ spec(Σ+Δ,0)
;   β: INT_ω unconditionally — interrogation has no retry

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
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
