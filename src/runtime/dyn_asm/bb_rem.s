; bb_rem.s   _XSTAR      match entire remainder; no backtrack
; spec_t  bb_rem(void *zeta, int entry)
;   rdi = zeta (ignored)   esi = entry
; rem_t: { int dummy @0 }  — state unused

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
global bb_rem

bb_rem:
        cmp     esi, 0
        je      REM_α
        jmp     REM_β
REM_α:  ; REM = spec(Σ+Δ, Ω-Δ);  Δ = Ω
        mov     rax, qword [rel Σ]      ; rax = Σ
        movsxd  rcx, dword [rel Δ]      ; rcx = old Δ
        add     rax, rcx                ; rax = σ = Σ+Δ
        mov     ecx, dword [rel Ω]
        sub     ecx, dword [rel Δ]      ; ecx = δ = Ω-Δ
        movsxd  rdx, ecx               ; rdx = δ
        ; Δ = Ω
        mov     ecx, dword [rel Ω]
        mov     dword [rel Δ], ecx
        jmp     REM_γ                   ; rax=σ, rdx=δ
REM_β:                                  jmp     REM_ω
REM_γ:                                  ret
REM_ω:  xor     eax, eax
        xor     edx, edx
        ret
