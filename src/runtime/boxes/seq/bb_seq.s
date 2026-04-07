; bb_seq.s   _XCAT       concatenation: left then right; β retries right then left
; spec_t  bb_seq(void *zeta, int entry)
;   rdi = zeta (seq_t*)    esi = entry
; seq_t: { bb_child_t left @0{fn@0,state@8}; bb_child_t right @16{fn@16,state@24}; spec_t matched @32{σ@32,δ@40} }
; bb_child_t: { void *fn @0; void *state @8 }   sizeof=16

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
global bb_seq

; Callee-saved: rbx=ζ, r12=lr.σ, r13=lr.δ, r14=rr.σ, r15=rr.δ
bb_seq:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 8                  ; align
        mov     rbx, rdi                ; rbx = ζ (seq_t*)
        cmp     esi, 0
        je      SEQ_α
        jmp     SEQ_β

SEQ_α:  ; ζ->matched = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx
        mov     qword [rbx+32], rax     ; ζ->matched.σ = Σ+Δ
        mov     dword [rbx+40], 0       ; ζ->matched.δ = 0
        ; lr = ζ->left.fn(ζ->left.state, α)
        mov     rdi, qword [rbx+8]      ; ζ->left.state
        xor     esi, esi                ; α=0
        call    qword [rbx+0]           ; ζ->left.fn(...)
        mov     r12, rax                ; lr.σ
        movsxd  r13, edx                ; lr.δ
        test    r12, r12
        jz      left_ω
        jmp     left_γ

SEQ_β:  ; rr = ζ->right.fn(ζ->right.state, β)
        mov     rdi, qword [rbx+24]     ; ζ->right.state
        mov     esi, 1                  ; β=1
        call    qword [rbx+16]          ; ζ->right.fn(...)
        mov     r14, rax                ; rr.σ
        movsxd  r15, edx                ; rr.δ
        test    r14, r14
        jz      right_ω
        jmp     right_γ

left_γ: ; ζ->matched = spec_cat(ζ->matched, lr)
        ; spec_cat: same σ, sum δ
        mov     eax, dword [rbx+40]
        add     eax, r13d               ; ζ->matched.δ += lr.δ
        mov     dword [rbx+40], eax
        ; rr = ζ->right.fn(ζ->right.state, α)
        mov     rdi, qword [rbx+24]
        xor     esi, esi
        call    qword [rbx+16]
        mov     r14, rax
        movsxd  r15, edx
        test    r14, r14
        jz      right_ω
        jmp     right_γ

left_ω: jmp     SEQ_ω

right_γ:; SEQ = spec_cat(ζ->matched, rr)
        mov     rax, qword [rbx+32]     ; ζ->matched.σ
        mov     ecx, dword [rbx+40]
        add     ecx, r15d               ; ζ->matched.δ + rr.δ
        movsxd  rdx, ecx
        jmp     SEQ_γ

right_ω:; lr = ζ->left.fn(ζ->left.state, β)
        ; first undo the matched.δ accumulation from left_γ
        mov     eax, dword [rbx+40]
        sub     eax, r13d               ; undo lr contribution
        mov     dword [rbx+40], eax
        mov     rdi, qword [rbx+8]
        mov     esi, 1                  ; β
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      left_ω
        jmp     left_γ

SEQ_γ:  add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
SEQ_ω:  xor     eax, eax
        xor     edx, edx
        add     rsp, 8
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
