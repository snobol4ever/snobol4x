; bb_pos.s   _XPOSI      assert cursor == n (zero-width)
; spec_t  bb_pos(void *zeta, int entry)
;   rdi = zeta (pos_t*)    esi = entry
; pos_t: { int n @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
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
