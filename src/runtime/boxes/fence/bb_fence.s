; bb_fence.s  _XFNCE     succeed once; β cuts (no retry)
; spec_t  bb_fence(void *zeta, int entry)
;   rdi = zeta (fence_t*)  esi = entry
; fence_t: { int fired @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
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
