/*
 * sil_macros.h — C translations of SIL macro instructions (v311.sil)
 *
 * Two axes:
 *   scrip-interp axis: macros/inlines used directly in C RT functions
 *     (snobol4.c, argval.c, invoke.c, nmd.c, eval_code.c, stmt_exec.c)
 *   SM_Program axis: the SM_INCR/SM_DECR/SM_ACOMP/SM_RCOMP/SM_LCOMP/
 *     SM_TRIM/SM_SPCINT/SM_SPREAL dispatch cases in sm_interp.c call
 *     the inline functions defined here. The x86 emitter writes assembly
 *     instructions (call lexcmp, cmp rax rbx, etc.) — not C — that
 *     produce identical behaviour.
 *
 * C translations verified against csnobol4 include/macros.h and the
 * generated snobol4.c (from genc.sno translating v311.sil).
 *
 * Our DESCR_t differs from csnobol4's descr:
 *   - no .f (flags) field — FNC dispatch handled separately via invoke.c
 *   - .slen replaces specifier length in csnobol4's SPEC struct
 *   - union fields: .s .i .r .p .arr .tbl .u .ptr
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-05
 */

#ifndef SIL_MACROS_H
#define SIL_MACROS_H

#include <stdint.h>
#include <string.h>   /* memcpy, memmove */
#include <math.h>     /* isfinite */

#include "snobol4.h"  /* DESCR_t, DTYPE_t, DT_* constants, FAILDESCR, NULVCL */

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 1 — Descriptor access  (scrip-interp axis only; too low-level for SM)
 *
 * SIL: GETD d,base,off   PUTD base,off,d   MOVD dst,src   etc.
 * csnobol4: D(x), D_A(x), D_V(x), D_F(x) in macros.h
 * Our equivalent: direct struct field access on DESCR_t.
 * ═══════════════════════════════════════════════════════════════════════ */

/* MOVD dst,src — copy full descriptor */
#define MOVD(dst, src)         ((dst) = (src))

/* MOVV dst,src — copy type tag only (SIL v field) */
#define MOVV(dst, src)         ((dst).v = (src).v)

/* MOVA dst,src — copy pointer field (SIL a field) */
#define MOVA(dst, src)         ((dst).ptr = (src).ptr)

/* SETAC d,val — set pointer field to integer constant */
#define SETAC(d, val)          ((d).ptr = (void *)(intptr_t)(val))

/* SETAV d,src — set pointer from type tag of src (SIL: d.a = src.v) */
#define SETAV(d, src)          ((d).ptr = (void *)(intptr_t)(src).v)

/* GETDC d,base,off — load descriptor from struct at byte offset off
 * SIL: GETDC d,base,ATTRIB etc.  In our model: field of a known C struct.
 * Use direct field access (e.g. nv->value) rather than this macro where
 * the struct type is known; keep GETDC for generic offset arithmetic. */
#define GETDC(d, base, off) \
    ((d) = *((DESCR_t *)((char *)(base) + (off))))

/* PUTDC base,off,d — store descriptor at byte offset */
#define PUTDC(base, off, d) \
    (*((DESCR_t *)((char *)(base) + (off))) = (d))

/* MOVBLK dst,src,sz — copy block of sz bytes (SIL: MOVBLK uses memmove) */
#define MOVBLK(dst, src, sz)   (memmove((void *)(dst), (void *)(src), (size_t)(sz)))

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 2 — Type tests and comparisons
 *
 * scrip-interp axis: C conditionals in RT functions.
 * SM_Program axis: SM_ACOMP, SM_RCOMP, SM_LCOMP dispatch to ACOMP/RCOMP
 *   defined below; the x86 emitter writes cmp/ucomisd assembly directly.
 * ═══════════════════════════════════════════════════════════════════════ */

/* TESTF d,FNC — test for function descriptor flag.
 * csnobol4 has a .f flags field; we handle FNC dispatch in invoke.c.
 * IS_FNC is a stub — always false at this layer (INVOKE table used instead). */
#define IS_FNC(d)              (0)   /* FNC bit not in our DESCR_t; invoke.c handles */

/* VEQLC d,T — value (type) equals constant T  (SIL: VEQLC d,S,t,f) */
#define VEQLC(d, T)            ((d).v == (T))

/* VEQL d1,d2 — type tags equal */
#define VEQL(a, b)             ((a).v == (b).v)

/* DEQL d1,d2 — full descriptor identity (SIL DEQL: same v and a fields) */
#define DEQL(a, b)             ((a).v == (b).v && (a).ptr == (b).ptr)

/* AEQLC d,val — address field equals integer constant */
#define AEQLC(d, val)          ((intptr_t)(d).ptr == (intptr_t)(val))

/* AEQL d1,d2 — address fields equal */
#define AEQL(a, b)             ((a).ptr == (b).ptr)

/* Type shorthands — used everywhere in RT functions */
#define IS_INT(d)    ((d).v == DT_I)
#define IS_REAL(d)   ((d).v == DT_R)
#define IS_STR(d)    ((d).v == DT_S || (d).v == DT_SNUL)
#define IS_PAT(d)    ((d).v == DT_P)
#define IS_NAME(d)   ((d).v == DT_N)
#define IS_KW(d)     ((d).v == DT_K)
#define IS_EXPR(d)   ((d).v == DT_E)
#define IS_CODE(d)   ((d).v == DT_C)
#define IS_ARR(d)    ((d).v == DT_A)
#define IS_TBL(d)    ((d).v == DT_T)
#define IS_FAIL(d)   ((d).v == DT_FAIL)
#define IS_NULL(d)   ((d).v == DT_SNUL)

/* ACOMP d1,d2 — compare integer/address fields; returns -1/0/+1
 * SIL: ACOMP d1,d2,lt,eq,gt  (three-way branch)
 * scrip-interp: call ACOMP(), branch on result.
 * SM_ACOMP dispatch: calls this, pushes result, SM_JUMP_S/F on it.
 * x86 emitter: writes  mov rax,[a.i] / cmp rax,[b.i] / setg/setl sequence. */
static inline int ACOMP(DESCR_t a, DESCR_t b) {
    return (a.i > b.i) - (a.i < b.i);
}

/* ACOMPC d,val — compare integer field against compile-time constant */
#define ACOMPC(d, val) \
    (((d).i > (int64_t)(val)) - ((d).i < (int64_t)(val)))

/* RCOMP d1,d2 — compare real fields; returns -1/0/+1
 * SM_RCOMP dispatch calls this.
 * x86 emitter: writes ucomisd xmm0,xmm1 / seta/setb sequence. */
static inline int RCOMP(DESCR_t a, DESCR_t b) {
    return (a.r > b.r) - (a.r < b.r);
}

/* PCOMP d,val — compare pointer field unsigned (for address range checks) */
#define PCOMP(d, val) \
    (((uintptr_t)(d).ptr > (uintptr_t)(val)) - \
     ((uintptr_t)(d).ptr < (uintptr_t)(val)))

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 3 — Arithmetic on addresses/integers
 *
 * scrip-interp axis: inline C arithmetic.
 * SM_Program axis: SM_INCR n / SM_DECR n dispatch to INCRA/DECRA.
 *   x86 emitter: writes  add rax,imm  /  sub rax,imm  directly.
 * ═══════════════════════════════════════════════════════════════════════ */

/* INCRA d,n — increment integer field by n  (SIL: very hot — every instruction fetch) */
#define INCRA(d, n)   ((d).i += (int64_t)(n))

/* DECRA d,n — decrement integer field by n */
#define DECRA(d, n)   ((d).i -= (int64_t)(n))

/* ADDLG d,sp — add string length to integer field */
#define ADDLG(d, s, slen)   ((d).i += (int64_t)(slen))

/* Integer/real conversions — RT functions, declared here, defined in argval.c */
DESCR_t INTRL_fn(DESCR_t d);           /* SIL INTRL: integer → real        */
DESCR_t RLINT_fn(DESCR_t d);           /* SIL RLINT: real → integer or FAIL */
DESCR_t NEG_I_fn(DESCR_t d);           /* SIL MNSINT: negate integer        */
DESCR_t NEG_R_fn(DESCR_t d);           /* SIL MNREAL: negate real           */
DESCR_t EXP_R_fn(DESCR_t base, DESCR_t exp); /* SIL EXREAL: real ** real    */

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 4 — String / specifier operations
 *
 * In our model: strings are (const char *s, size_t len) pairs extracted
 * from DESCR_t via descr_slen() and .s.  No separate SPEC struct.
 *
 * scrip-interp axis: C function calls.
 * SM_Program axis: SM_TRIM, SM_SPCINT, SM_SPREAL dispatch to fns below.
 *   x86 emitter: writes  call trimsp / call spcint / call spreal  +
 *   conditional branch on return value (assembly call + test + jz).
 * ═══════════════════════════════════════════════════════════════════════ */

/* LOCSP — extract (ptr, len) from DESCR_t
 * SIL: LOCSP sp,d  gets specifier from descriptor.
 * Usage: const char *p; size_t len; LOCSP(p, len, d); */
#define LOCSP(ptr_out, len_out, d) \
    do { \
        (ptr_out) = (d).s; \
        (len_out) = descr_slen(d); \
    } while (0)

/* GETLG — get string length from descriptor */
#define GETLG(d)   ((int64_t)descr_slen(d))

/* LEQLC sp,n — string length equals constant n */
#define LEQLC(d, n)   (descr_slen(d) == (size_t)(n))

/* TRIMSP — trim trailing blanks (SIL: TRIMSP sp1,sp2)
 * SM_TRIM dispatch calls TRIM_fn.
 * x86 emitter: call trimsp  (assembly call instruction). */
DESCR_t TRIM_fn(DESCR_t d);            /* → DT_S with trailing spaces removed */

/* SPCINT — parse integer from string (SIL: SPCINT d,sp,fail,ok)
 * Returns 1 on success (d set to DT_I), 0 on failure.
 * SM_SPCINT operand carries f_label; dispatch: if (!SPCINT_fn(&d,s,l)) goto f.
 * x86 emitter: call spcint / test eax,eax / jz f_label. */
int SPCINT_fn(DESCR_t *out, const char *s, size_t len);

/* SPREAL — parse real from string (SIL: SPREAL d,sp,fail,ok)
 * Returns 1 on success (out set to DT_R), 0 on failure.
 * SM_SPREAL: same pattern as SM_SPCINT. */
int SPREAL_fn(DESCR_t *out, const char *s, size_t len);

/* REALST — format real to string (SIL: REALST sp,d) */
DESCR_t REALST_fn(DESCR_t d);          /* → DT_S representation of real     */

/* INTSP — format integer to string (SIL: INTSP sp,d / INTSPC) */
DESCR_t INTSP_fn(DESCR_t d);           /* → DT_S representation of integer  */

/* LCOMP — lexicographic string compare; returns -1/0/+1
 * SM_LCOMP dispatch calls LCOMP_fn.
 * x86 emitter: call lexcmp (assembly call) then test eax / jg jl etc. */
int LCOMP_fn(const char *s1, size_t l1, const char *s2, size_t l2);

/* LEXEQ — fast string equality (no branching on length first) */
#define LEXEQ(s1, l1, s2, l2) \
    ((l1) == (l2) && ((l1) == 0 || memcmp((s1), (s2), (l1)) == 0))

/* SUBSTR — extract substring (signature matches snobol4.h) */
/* DESCR_t SUBSTR_fn(DESCR_t s, DESCR_t i, DESCR_t n); — declared in snobol4.h */

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 5 — Control flow
 *
 * BRANCH/RCALL/RRTURN map to SM_JUMP/SM_CALL/SM_RETURN (DONE in SCRIP-SM.md).
 * The new ones below are the three additions from the SIL scan.
 * ═══════════════════════════════════════════════════════════════════════ */

/* SM_JUMP_INDIR — SIL BRANIC d,0
 * scrip-interp: pc = (SM_Instr *)d.ptr; continue;
 * x86 emitter:  jmp [rax]   (FF E0 — indirect jump through register) */
/* (no C macro needed; sm_interp.c handles inline) */

/* SM_SELBRA — SIL SELBRA d,table
 * scrip-interp: computed goto via table[d.i]
 * x86 emitter:  jmp [table + rax*8]  (jump table in .rodata) */
/* (no C macro needed; sm_interp.c handles with switch or computed goto) */

/* SM_STATE_PUSH / SM_STATE_POP — SIL ISTACKPUSH / restore
 * Used by EXPVAL (RT-6) to save/restore the full interpreter register file
 * before executing a nested EXPRESSION.
 * scrip-interp: memcpy register snapshot to a save-stack.
 * x86 emitter:  call state_push  /  call state_pop  (assembly calls). */
void state_push(void);   /* save OCBSCL,OCICL,NAMICL,NHEDCL,PDLPTR,PDLHED + specifiers */
void state_pop(void);    /* restore from save stack */

/* SPUSH / SPOP — push/pop specifier (2 descriptors wide) onto cstack
 * Used inside state_push/state_pop for HEADSP,TSP,TXSP,XSP.
 * Not SM instructions — internal to state_push/state_pop implementations. */
/* (implemented in eval_code.c) */

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 8 helpers — convenience wrappers matching SIL procedure names
 *
 * These are declared in snobol4.h; repeated here as a quick-reference
 * cross-index to their SIL names so RT implementors can grep sil_macros.h.
 *
 * SIL INVOKE   → INVOKE_fn()   in invoke.c
 * SIL ARGVAL   → ARGVAL_fn()   in argval.c
 * SIL VARVAL   → VARVAL_d_fn() in argval.c
 * SIL INTVAL   → INTVAL_fn()   in argval.c
 * SIL PATVAL   → PATVAL_fn()   in argval.c
 * SIL VARVUP   → VARVUP_fn()   in argval.c
 * SIL NAME     → NAME_fn()     in snobol4.c   (RT-3)
 * SIL ASGN     → ASGN_fn()     in snobol4.c   (RT-5)
 * SIL ASGNIC   → ASGNIC_fn()   in snobol4.c   (RT-3)
 * SIL NMD      → NAM_commit()  in nmd.c        (RT-4)
 * SIL EXPVAL   → EXPVAL_fn()   in eval_code.c  (RT-6)
 * SIL EXPEVL   → EXPEVL_fn()   in eval_code.c  (RT-6)
 * SIL CONVE    → CONVE_fn()    in snobol4.c    (RT-7)
 * SIL CODER    → CODE_fn()     in snobol4.c    (RT-7)
 * SIL CNVRT    → CONVERT_fn()  in snobol4.c    (RT-7)
 * SIL EVAL     → EVAL_fn()     in snobol4.c    (RT-8)
 * ═══════════════════════════════════════════════════════════════════════ */

/* NAME_fn, ASGNIC_fn — declared here for RT-3 implementors */
DESCR_t NAME_fn(const char *varname);          /* SIL NAME: .X → DT_N descriptor */
int     ASGNIC_fn(const char *kw, DESCR_t v); /* SIL ASGNIC: keyword assign via INTVAL */

/* EXPVAL_fn, EXPEVL_fn — declared here for RT-6 implementors */
DESCR_t EXPVAL_fn(DESCR_t expr_d);   /* SIL EXPVAL: execute EXPRESSION, return value */
DESCR_t EXPEVL_fn(DESCR_t expr_d);  /* SIL EXPEVL: execute EXPRESSION, return by name */

/* CONVE_fn — declared here for RT-7 implementors */
DESCR_t CONVE_fn(DESCR_t str_d);    /* SIL CONVE: string → DT_E EXPRESSION */

#endif /* SIL_MACROS_H */
