; bb_eps.s   _XEPS       zero-width success once; done flag prevents double-γ
; spec_t  bb_eps(void *zeta, int entry)
;   rdi = zeta (eps_t*)    esi = entry (0=α, 1=β)
; eps_t: { int done @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
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
