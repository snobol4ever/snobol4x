; bb_rpos.s  _XRPSI      assert cursor == Ω-n (zero-width)
; spec_t  bb_rpos(void *zeta, int entry)
;   rdi = zeta (rpos_t*)   esi = entry
; rpos_t: { int n @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
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
