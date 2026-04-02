; bb_succeed.s  _XSUCF    always γ zero-width; outer loop retries
; spec_t  bb_succeed(void *zeta, int entry)
;   rdi = zeta (ignored)   esi = entry (ignored)

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
global bb_succeed

bb_succeed:
        mov     rax, qword [rel Σ]      ; σ = Σ
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        ret                             ; return spec(Σ+Δ, 0)
