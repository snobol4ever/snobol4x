; bb_dvar.s  _XDSAR/_XVAR  *VAR/VAR — re-resolve live value on every α
; spec_t  bb_deferred_var(void *zeta, int entry)
;   rdi = zeta (dvar_t*)   esi = entry
; dvar_t: { bb_box_fn child_fn @0; void *child_state @8;
;            size_t child_size @16; const char *name @24 }
; NV_GET_fn(const char *name) → DESCR_t (rax=low qword, rdx=high qword)
;   DESCR_t: { DTYPE_t v @0 (int); uint32_t slen @4; union ptr/i @8 }
;   DT_P=3, DT_S=1
; bb_build(void *patnd) → bb_node_t { bb_box_fn fn @0; void *ζ @8; size_t ζ_size @16 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern NV_GET_fn
extern bb_build
extern bb_lit
extern calloc
extern memset

section .text
global bb_deferred_var

; Local frame layout (sub rsp, 48):
;   [rsp+ 0] DESCR_t val  (16 bytes: low @0, high @8)
;   [rsp+16] bb_node_t c  (24 bytes: fn@16, ζ@24, ζ_size@32)  [only 24 used]
;   [rsp+40] pad

bb_deferred_var:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        sub     rsp, 48
        mov     rbx, rdi                ; rbx = ζ (dvar_t*)
        cmp     esi, 0
        je      DVAR_α
        jmp     DVAR_β

DVAR_α: ; val = NV_GET_fn(ζ->name)
        mov     rdi, qword [rbx+24]     ; ζ->name
        call    NV_GET_fn
        mov     qword [rsp+0], rax      ; val low
        mov     qword [rsp+8], rdx      ; val high
        ; int rebuilt = 0
        xor     r15d, r15d
        ; check DT_P: val.v == 3
        mov     eax, dword [rsp+0]      ; val.v (low dword of low qword)
        cmp     eax, 3                  ; DT_P ?
        jne     .check_dts
        ; val.p (ptr) = high qword
        mov     r12, qword [rsp+8]      ; val.p
        test    r12, r12
        jz      .check_dts
        ; if val.p != ζ->child_state → rebuild
        cmp     r12, qword [rbx+8]
        je      .check_dts
        ; c = bb_build(val.p)
        mov     rdi, r12
        call    bb_build
        ; bb_node_t returned: rax=fn, rdx=ζ ptr ... wait, bb_build returns struct
        ; bb_build returns bb_node_t (24 bytes) — check actual ABI
        ; Since bb_node_t > 16 bytes it uses sret: first arg is hidden ptr
        ; We need to pass &c as hidden first arg
        ; Actually: struct > 16 bytes → hidden pointer in rdi, real arg in rsi
        ; Redo: pass hidden ptr first
        lea     rdi, [rsp+16]           ; &c (hidden sret)
        mov     rsi, r12                ; val.p
        call    bb_build
        mov     r13, qword [rsp+16]     ; c.fn
        mov     r14, qword [rsp+24]     ; c.ζ
        mov     qword [rbx+0], r13      ; ζ->child_fn = c.fn
        mov     qword [rbx+8], r14      ; ζ->child_state = c.ζ
        mov     rax, qword [rsp+32]     ; c.ζ_size
        mov     qword [rbx+16], rax     ; ζ->child_size = c.ζ_size
        mov     r15d, 1                 ; rebuilt = 1
        jmp     .check_reset
.check_dts:
        ; check DT_S: val.v == 1
        mov     eax, dword [rsp+0]
        cmp     eax, 1                  ; DT_S ?
        jne     .check_reset
        mov     r12, qword [rsp+8]      ; val.s (string ptr, high qword)
        test    r12, r12
        jz      .check_reset
        ; if (!lz || lz->lit != val.s) rebuild as lit
        mov     r14, qword [rbx+8]      ; ζ->child_state (lz)
        test    r14, r14
        jz      .rebuild_lit
        ; lz->lit is at offset 0 (lit_t: { const char *lit @0; int len @8 })
        cmp     qword [r14+0], r12      ; lz->lit == val.s?
        je      .check_reset
.rebuild_lit:
        ; lz = calloc(1, 16)  [sizeof(lit_t)=16]
        mov     rdi, 1
        mov     rsi, 16
        call    calloc
        mov     r14, rax                ; lz
        mov     qword [r14+0], r12      ; lz->lit = val.s
        ; lz->len = strlen(val.s) — use slen field from DESCR_t (dword @4 of low qword)
        mov     eax, dword [rsp+4]      ; val.slen
        mov     dword [r14+8], eax      ; lz->len = slen
        mov     qword [rbx+0], bb_lit   ; ζ->child_fn = bb_lit
        mov     qword [rbx+8], r14      ; ζ->child_state = lz
        mov     qword [rbx+16], 16      ; ζ->child_size = sizeof(lit_t)
        mov     r15d, 1                 ; rebuilt = 1
.check_reset:
        ; if (!rebuilt && ζ->child_state && ζ->child_size && ζ->child_fn != bb_lit)
        test    r15d, r15d
        jnz     .call_child
        cmp     qword [rbx+8], 0
        je      .call_child
        cmp     qword [rbx+16], 0
        je      .call_child
        mov     rax, qword [rbx+0]
        lea     rcx, [rel bb_lit]
        cmp     rax, rcx
        je      .call_child
        ; memset(ζ->child_state, 0, ζ->child_size)
        mov     rdi, qword [rbx+8]
        xor     esi, esi
        mov     rdx, qword [rbx+16]
        call    memset
.call_child:
        ; if (!ζ->child_fn) goto ω
        cmp     qword [rbx+0], 0
        je      DVAR_ω
        ; DVAR = ζ->child_fn(ζ->child_state, α)
        mov     rdi, qword [rbx+8]
        xor     esi, esi
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      DVAR_ω
        jmp     DVAR_γ

DVAR_β: ; if (!ζ->child_fn) goto ω
        cmp     qword [rbx+0], 0
        je      DVAR_ω
        ; DVAR = ζ->child_fn(ζ->child_state, β)
        mov     rdi, qword [rbx+8]
        mov     esi, 1
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      DVAR_ω
        jmp     DVAR_γ

DVAR_γ: mov     rax, r12
        movsxd  rdx, r13d
        add     rsp, 48
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
DVAR_ω: xor     eax, eax
        xor     edx, edx
        add     rsp, 48
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret
