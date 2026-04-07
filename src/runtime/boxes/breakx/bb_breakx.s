; bb_breakx.s _XBRKX     like BRK but fails on zero advance
; spec_t  bb_breakx(void *zeta, int entry)
;   rdi = zeta (brkx_t*)   esi = entry
; brkx_t: { const char *chars @0; int δ @8 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω
extern strchr

section .text
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
