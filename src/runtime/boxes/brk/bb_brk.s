; bb_brk.s   _XBRKC      scan to first char in set (may be zero-width)
; spec_t  bb_brk(void *zeta, int entry)
;   rdi = zeta (brk_t*)    esi = entry
; brk_t: { const char *chars @0; int δ @8 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω
extern strchr

section .text
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
