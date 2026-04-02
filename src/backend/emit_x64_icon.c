/*
 * emit_x64_icon.c — Icon IR (EXPR_t) → x64 NASM emitter (M-G9-ICON-IR-WIRE)
 *
 */
#define _POSIX_C_SOURCE 200809L
/*
 * Tier 0 (Rung 1-2): E_ILIT, E_QLIT, E_ADD/SUB/MUL/DIV/MOD,
 *   E_LT/LE/GT/GE/EQ/NE, ICN_TO/TO_BY, ICN_EVERY, E_FNC(write), E_FNC
 *
 * Tier 1 (Rung 3): user procedures with params/locals, ICN_RETURN, ICN_FAIL,
 *   E_VAR (local/param), user function calls, E_ASSIGN, ICN_IF
 *
 * Calling convention for user procs:
 *   - Args pushed onto icn_stack (rightmost first) before call
 *   - Callee pops args, stores in rbx-saved frame slots (rbp-relative)
 *   - Return value: icn_retval global; icn_failed=0 on return, 1 on fail
 *   - Local vars: frame slots above params (rbp - 8*(param+local+1))
 */

#include "../../ir/ir.h"
#include "../snobol4/scrip_cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* =========================================================================
 * Module globals — set by emit_x64_icon_file() before any emission
 * ======================================================================= */
static FILE *out;
static int   uid = 0;
static int   next_uid(void) { return uid++; }

/* =========================================================================
 * Output helpers
 * ======================================================================= */
static void E(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
}
static void Ldef(const char *l) { fprintf(out, "%s:\n", l); }
static void Jmp (const char *t) { fprintf(out, "    jmp     %s\n", t); }

/* =========================================================================
 * Label utilities
 * ======================================================================= */

void icn_label_α(int id, char *b, size_t s)          { snprintf(b,s,"icn_%d_α",id); }
void icn_label_β (int id, char *b, size_t s)          { snprintf(b,s,"icn_%d_β",id); }
void icn_lbl_code (int id, char *b, size_t s)          { snprintf(b,s,"icn_%d_code",id); }
static void label_val(int id, char *b, size_t s)         { snprintf(b,s,"icn_%d_val",id); }
static void label_I  (int id, char *b, size_t s)         { snprintf(b,s,"icn_%d_I",id); }

/* =========================================================================
 * BSS / rodata
 * ======================================================================= */
#define MAX_BSS    512
#define MAX_RODATA 256
typedef struct { char name[64]; }           BssEntry;
typedef struct { char name[64]; char *data;} RodataEntry;
static BssEntry    bss_entries[MAX_BSS];    static int bss_count=0;
static RodataEntry rodata_entries[MAX_RODATA]; static int rodata_count=0;
static int         str_counter=0;

static void bss_declare(const char *name) {
    for (int i=0;i<bss_count;i++) if(!strcmp(bss_entries[i].name,name)) return;
    if(bss_count<MAX_BSS) strncpy(bss_entries[bss_count++].name,name,63);
}
static void rodata_declare(const char *label, const char *data) {
    for (int i=0;i<rodata_count;i++) if(!strcmp(rodata_entries[i].name,label)) return;
    if(rodata_count>=MAX_RODATA) return;
    int i=rodata_count++;
    strncpy(rodata_entries[i].name,label,63);
    rodata_entries[i].data=strdup(data);
}
static void alloc_str_label(char *b, size_t s) { snprintf(b,s,"icn_str_%d",str_counter++); }

/* =========================================================================
 * User procedure registry
 * ======================================================================= */
#define MAX_USER_PROCS 64
static char user_procs[MAX_USER_PROCS][64];
static int  user_proc_nparams[MAX_USER_PROCS];
static int  user_proc_is_gen[MAX_USER_PROCS];  /* 1 if proc contains E_SUSPEND */
static int  user_proc_count=0;

static void icn_register_proc(const char *name, int nparams, int is_gen) {
    for(int i=0;i<user_proc_count;i++) if(!strcmp(user_procs[i],name)) return;
    if(user_proc_count<MAX_USER_PROCS){
        strncpy(user_procs[user_proc_count],name,63);
        user_proc_nparams[user_proc_count]=nparams;
        user_proc_is_gen[user_proc_count]=is_gen;
        user_proc_count++;
    }
}
static int icn_is_user_proc(const char *name) {
    for(int i=0;i<user_proc_count;i++) if(!strcmp(user_procs[i],name)) return 1;
    return 0;
}
static int icn_is_gen_proc(const char *name) {
    for(int i=0;i<user_proc_count;i++)
        if(!strcmp(user_procs[i],name)) return user_proc_is_gen[i];
    return 0;
}

/* Recursively check if any node in the tree is E_SUSPEND */
static int has_suspend(EXPR_t *n) {
    if(!n) return 0;
    if(n->kind==E_SUSPEND) return 1;
    for(int i=0;i<n->nchildren;i++)
        if(has_suspend(n->children[i])) return 1;
    return 0;
}

/* =========================================================================
 * Per-procedure local variable table
 * ======================================================================= */
#define MAX_LOCALS 32
/* type: '?' unknown, 'I' integer, 'S' string/cset pointer */
typedef struct { char name[32]; int slot; char type; } LocalVar;
static LocalVar cur_locals[MAX_LOCALS];
static int      cur_nlocals=0, cur_nparams=0;
static char     cur_ret_label[64]="";   /* label to jump to for return */
static char     cur_fail_label[64]="";  /* label to jump to for fail */
static char     cur_suspend_ret_label[64]=""; /* bare ret — frame kept alive for suspend */

static void locals_reset(void) { cur_nlocals=0; cur_nparams=0; }

/* =========================================================================
 * BSS global variable type table (cross-procedure, persists for file)
 * ======================================================================= */
#define MAX_GVARS 64
typedef struct { char name[32]; char type; } GVar;
static GVar gvar_types[MAX_GVARS];
static int  gvar_count = 0;

static void globals_set_type(const char *name, char type) {
    for (int i = 0; i < gvar_count; i++)
        if (!strcmp(gvar_types[i].name, name)) { gvar_types[i].type = type; return; }
    if (gvar_count < MAX_GVARS) {
        strncpy(gvar_types[gvar_count].name, name, 31);
        gvar_types[gvar_count].type = type;
        gvar_count++;
    }
}
static char globals_type(const char *name) {
    for (int i = 0; i < gvar_count; i++)
        if (!strcmp(gvar_types[i].name, name)) return gvar_types[i].type;
    return '?';
}

/* Allocate an anonymous frame slot for a temporary (e.g. binop/relop lcache).
 * Returns the slot index; use slot_offset(slot) for rbp-relative access. */
static int locals_alloc_tmp(void) {
    int slot=cur_nlocals;
    if(cur_nlocals<MAX_LOCALS){
        cur_locals[cur_nlocals].name[0]='\0';
        cur_locals[cur_nlocals].type='?';
        cur_locals[cur_nlocals++].slot=slot;
    }
    return slot;
}

static int locals_find(const char *name) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)) return cur_locals[i].slot;
    return -1;
}
static int locals_add(const char *name) {
    int slot=cur_nlocals;
    if(cur_nlocals<MAX_LOCALS){
        strncpy(cur_locals[cur_nlocals].name,name,31);
        cur_locals[cur_nlocals].type='?';
        cur_locals[cur_nlocals++].slot=slot;
    }
    return slot;
}
static void locals_set_type(const char *name, char type) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)){ cur_locals[i].type=type; return; }
}
static char locals_type(const char *name) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)) return cur_locals[i].type;
    return '?';
}
/* Frame offset for slot N: rbp - 8*(N+1) */
static int slot_offset(int slot) { return -8*(slot+1); }

/* Walk an expression node and return its Tiny-ICON value kind:
 * 'S' = string/cset pointer, 'I' = integer, '?' = unknown. */
static char icn_expr_kind(EXPR_t *n) {
    if (!n) return '?';
    switch (n->kind) {
        case E_QLIT:    return 'S';
        case E_CSET:   return 'S';
        case E_ILIT:    return 'I';
        case E_CAT: case E_LCONCAT: return 'S';
        case E_ADD: case E_SUB: case E_MUL:
        case E_DIV: case E_MOD:        return 'I';
        case E_VAR:
            if (strcmp(n->sval, "&subject") == 0) return 'S';
            if (strcmp(n->sval, "&pos")     == 0) return 'I';
            { char k = locals_type(n->sval); if (k != '?') return k; }
            return globals_type(n->sval);
        case E_FNC:
            /* Known string-returning builtins */
            if (n->nchildren >= 1 && n->children[0] &&
                n->children[0]->kind == E_VAR) {
                const char *fn = n->children[0]->sval;
                /* tab/move return substrings (char*); any/many/upto return long pos */
                if (strcmp(fn,"tab")==0   || strcmp(fn,"move")==0  ||
                    strcmp(fn,"string")==0 || strcmp(fn,"read")==0  ||
                    strcmp(fn,"reads")==0  || strcmp(fn,"repl")==0  ||
                    strcmp(fn,"reverse")==0)
                    return 'S';
                if (strcmp(fn,"any")==0  || strcmp(fn,"many")==0 ||
                    strcmp(fn,"upto")==0 || strcmp(fn,"match")==0 ||
                    strcmp(fn,"find")==0)
                    return 'I';
            }
            return '?';
        default:         return '?';
    }
}

/* Pre-pass: walk top-level and one level of nesting in proc body to record
 * rhs type for each E_ASSIGN so write() can dispatch correctly. */
static void infer_local_types(EXPR_t *proc, int body_start) {
    int nstmts = proc->nchildren - body_start;
    for (int si = 0; si < nstmts; si++) {
        EXPR_t *s = proc->children[body_start + si];
        if (!s) continue;
        if (s->kind == E_ASSIGN && s->nchildren >= 2) {
            EXPR_t *lhs = s->children[0], *rhs = s->children[1];
            if (lhs && lhs->kind == E_VAR) {
                char k = icn_expr_kind(rhs);
                if (k != '?') {
                    if (locals_find(lhs->sval) >= 0) locals_set_type(lhs->sval, k);
                    else                              globals_set_type(lhs->sval, k);
                }
            }
        }
        for (int ci = 0; ci < s->nchildren; ci++) {
            EXPR_t *c = s->children[ci];
            if (!c) continue;
            if (c->kind == E_ASSIGN && c->nchildren >= 2) {
                EXPR_t *lhs = c->children[0], *rhs = c->children[1];
                if (lhs && lhs->kind == E_VAR) {
                    char k = icn_expr_kind(rhs);
                    if (k != '?') {
                        if (locals_find(lhs->sval) >= 0) locals_set_type(lhs->sval, k);
                        else                              globals_set_type(lhs->sval, k);
                    }
                }
            }
        }
    }
}

/* =========================================================================
 * Forward declaration
 * ======================================================================= */
static void emit_expr(EXPR_t *n, const char *γ, const char *ω,
                      char *out_α, char *out_β);

/* Loop control stack — forward declarations (defined near emit_repeat) */
static void loop_push(const char *brk, const char *nxt);
static void loop_pop(void);

/* =========================================================================
 * E_ILIT
 * Stack protocol: α pushes value onto hardware stack then jumps succeed.
 * β pops nothing (re-entry after backtrack) then jumps fail.
 * ======================================================================= */
static void emit_int(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; INT %ld  id=%d\n",n->ival,id);
    Ldef(a); E("    push    %ld\n",n->ival); Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * E_QLIT
 * ======================================================================= */
static void emit_str(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64],sl[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    alloc_str_label(sl,sizeof sl); rodata_declare(sl,n->sval);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(a); E("    lea     rdi, [rel %s]\n",sl); Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * E_VAR — variable reference
 * α: load value into rax (from frame slot or global BSS), push, succeed.
 * β: jump fail (one-shot — variable has no next value).
 * ======================================================================= */
static void emit_var(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; VAR %s  id=%d\n",n->sval,id);
    Ldef(a);
    /* &subject / &pos keywords */
    if (strcmp(n->sval, "&subject") == 0) {
        bss_declare("icn_subject");
        E("    mov     rax, [rel icn_subject]\n");
        E("    push    rax\n");
        Jmp(γ);
        Ldef(b); Jmp(ω);
        return;
    }
    if (strcmp(n->sval, "&pos") == 0) {
        bss_declare("icn_pos");
        E("    mov     rax, [rel icn_pos]\n");
        E("    push    rax\n");
        Jmp(γ);
        Ldef(b); Jmp(ω);
        return;
    }
    int slot=locals_find(n->sval);
    if(slot>=0) {
        E("    mov     rax, [rbp%+d]\n", slot_offset(slot));
    } else {
        /* Global BSS var */
        char gv[80]; snprintf(gv,sizeof gv,"icn_gvar_%s",n->sval);
        bss_declare(gv);
        E("    mov     rax, [rel %s]\n", gv);
    }
    E("    push    rax\n");
    Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * E_ASSIGN — E1 := E2
 * Evaluates E2, stores result into E1 (must be E_VAR).
 * ======================================================================= */
static void emit_assign(EXPR_t *n, const char *γ, const char *ω,
                        char *oa, char *ob) {
    if(n->nchildren<2){ emit_expr(NULL, γ, ω, oa, ob); return; }
    int id=next_uid(); char a[64],b[64],store[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(store,sizeof store,"icon_%d_store",id);
    strncpy(oa,a,63); strncpy(ob,b,63);

    char rhs_γ[64]; char rhs_ω[64]; strncpy(rhs_γ,store,63); strncpy(rhs_ω,ω,63);
    char ra[64],rb[64];
    emit_expr(n->children[1], rhs_γ, rhs_ω, ra, rb);

    Ldef(a); Jmp(ra);
    Ldef(b); Jmp(rb);

    Ldef(store);
    /* E_QLIT/E_CSET leave pointer in rdi (nothing pushed); all others push a value */
    EXPR_t *rhs = n->children[1];
    int rhs_is_str = (rhs && (rhs->kind == E_QLIT || rhs->kind == E_CSET));
    if (rhs_is_str) {
        E("    ; str assign: rdi already has pointer\n");
    } else {
        E("    pop     rax\n"); /* consume value pushed by RHS */
    }

    EXPR_t *lhs=n->children[0];
    if(lhs && lhs->kind==E_VAR){
        int slot=locals_find(lhs->sval);
        if(slot>=0){
            if (rhs_is_str)
                E("    mov     [rbp%+d], rdi\n",slot_offset(slot));
            else
                E("    mov     [rbp%+d], rax\n",slot_offset(slot));
        } else {
            char gv[80]; snprintf(gv,sizeof gv,"icn_gvar_%s",lhs->sval);
            bss_declare(gv);
            if (rhs_is_str)
                E("    mov     [rel %s], rdi\n",gv);
            else
                E("    mov     [rel %s], rax\n",gv);
        }
    }
    Jmp(γ);
}

/* =========================================================================
 * ICN_RETURN
 * Stores value into icn_retval, jumps to cur_ret_label.
 * ======================================================================= */
static void emit_return(EXPR_t *n, const char *γ, const char *ω,
                        char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    if(n->nchildren>0){
        char after[64]; snprintf(after,sizeof after,"icon_%d_ret_store",id);
        char vp_γ[64]; char vp_ω[64]; strncpy(vp_γ,after,63); strncpy(vp_ω,after,63);
        char va2[64],vb2[64];
        emit_expr(n->children[0], vp_γ, vp_ω, va2, vb2);
        Ldef(a); Jmp(va2);
        Ldef(b); Jmp(cur_ret_label[0]?cur_ret_label:"icn_dead");
        Ldef(after);
        E("    pop     rax\n"); /* consume value pushed by expr */
        E("    mov     [rel icn_retval], rax\n");
        E("    mov     byte [rel icn_failed], 0\n");
        Jmp(cur_ret_label[0]?cur_ret_label:"icn_dead");
    } else {
        Ldef(a);
        E("    mov     qword [rel icn_retval], 0\n");
        E("    mov     byte [rel icn_failed], 0\n");
        Jmp(cur_ret_label[0]?cur_ret_label:"icn_dead");
        Ldef(b); Jmp(cur_ret_label[0]?cur_ret_label:"icn_dead");
    }
}

/* =========================================================================
 * ICN_FAIL — procedure failure
 * ======================================================================= */
static void emit_fail_node(EXPR_t *n, const char *γ, const char *ω,
                           char *oa, char *ob) {
    (void)n;    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(a);
    if(cur_fail_label[0]){
        E("    mov     byte [rel icn_failed], 1\n");
        Jmp(cur_fail_label);
    } else Jmp(ω);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * E_SUSPEND — co-routine yield (user-defined generator)
 *
 * suspend E [do body]
 *
 * α: evaluate E; on succeed → store value in icn_retval, store resume label
 *    address in icn_suspend_resume, jump to cur_ret_label (yield to caller).
 * β: jmp [rel icn_suspend_resume] — resume from where we left off.
 *    If there is a 'do body', β runs the body then resumes E's β.
 *    If E is exhausted (ω), procedure fails (jump cur_fail_label).
 *
 * Single BSS slot icn_suspend_resume holds the resume address.
 * This is a single-active-generator design — only one suspend active at a time.
 * ======================================================================= */
static void emit_suspend(EXPR_t *n, const char *γ, const char *ω,
                         char *oa, char *ob) {
    int id = next_uid();
    char a[64], b[64];
    icn_label_α(id, a, sizeof a);
    icn_label_β (id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    /* resume_here: label the caller's β jumps back to after we yield */
    char resume_here[64]; snprintf(resume_here, sizeof resume_here, "icon_%d_resume", id);
    /* after_val: entered after E succeeds (value on stack) */
    char after_val[64];   snprintf(after_val,   sizeof after_val,   "icon_%d_yield",  id);

    /* Declare BSS slots (deduped) */
    bss_declare("icn_suspend_resume");
    bss_declare("icn_suspended");      /* byte: 1 if proc suspended, 0 if returned/failed */

    EXPR_t *val_node  = (n->nchildren > 0) ? n->children[0] : NULL;
    EXPR_t *body_node = (n->nchildren > 1) ? n->children[1] : NULL;

    /* Emit the value expression; on success jump to after_val */
    char va[80], vb[80];
    if (val_node) {
        char vp_γ[64]; char vp_ω[64];
        strncpy(vp_γ, after_val, 63);
        strncpy(vp_ω, cur_fail_label[0] ? cur_fail_label : "icn_dead", 63);
        emit_expr(val_node, vp_γ, vp_ω, va, vb);
    } else {
        /* suspend with no value: yield 0 */
        snprintf(va, sizeof va, "%s_noval", a);
        snprintf(vb, sizeof vb, "%s_novalb", a);
        Ldef( va); E("    push    0\n"); Jmp( after_val);
        Ldef( vb); Jmp( cur_fail_label[0] ? cur_fail_label : "icn_dead");
    }

    /* α: jump into value evaluation */
    Ldef( a); Jmp( va);

    /* β: resume — jump through icn_suspend_resume slot */
    Ldef( b);
    E("    jmp     [rel icn_suspend_resume]\n");

    /* after_val: E succeeded, value on hw stack */
    Ldef( after_val);
    E("    pop     rax\n");
    E("    mov     [rel icn_retval], rax\n");
    E("    mov     byte [rel icn_failed], 0\n");
    E("    mov     byte [rel icn_suspended], 1\n");      /* signal: suspended, not returned */
    /* store resume address: after yield, caller's β comes back to resume_here */
    E("    lea     rax, [rel %s]\n", resume_here);
    E("    mov     [rel icn_suspend_resume], rax\n");
    E("    mov     [rel icn_suspend_rbp], rbp\n");
    /* yield to caller — bare ret, frame stays alive */
    Jmp( cur_suspend_ret_label[0] ? cur_suspend_ret_label : "icn_dead");

    /* resume_here: execution resumes here after caller's β fires.
     * ORDERING FIX: emit body nodes FIRST (they may emit sub-labels inline),
     * then define resume_here + jmp to body's α.  This ensures that
     * resume_here: is immediately followed by jmp ba — not by a sub-node label
     * that would cause fall-through into the wrong place. */
    if (body_node) {
        char ba[64], bb[64];
        char bp_γ[64]; char bp_ω[64];
        strncpy(bp_γ, γ, 63);
        strncpy(bp_ω,    γ, 63);  /* body fail also continues */
        emit_expr(body_node, bp_γ, bp_ω, ba, bb);
        Ldef( resume_here);
        Jmp( ba);
    } else {
        Ldef( resume_here);
        Jmp( γ);
    }
}

/* =========================================================================
 * ICN_IF — if cond then E2 [else E3]  (paper §4.5 indirect goto)
 * Simple version (no bounded optimization): emit cond, on succeed→E2, fail→E3/ω
 * ======================================================================= */
static void emit_if(EXPR_t *n, const char *γ, const char *ω,
                    char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    EXPR_t *cond=n->children[0];
    EXPR_t *thenb=(n->nchildren>1)?n->children[1]:NULL;
    EXPR_t *elseb=(n->nchildren>2)?n->children[2]:NULL;

    char then_a[64],then_b[64],else_a[64],else_b[64];
    char cond_then[64]; snprintf(cond_then,sizeof cond_then,"icon_%d_then",id);
    char cond_else[64]; snprintf(cond_else,sizeof cond_else,"icon_%d_else",id);

    if(thenb){
        char tp_γ[64]; char tp_ω[64]; strncpy(tp_γ,γ,63); strncpy(tp_ω,ω,63);
        emit_expr(thenb, tp_γ, tp_ω, then_a,then_b);
    } else { strncpy(then_a,γ,63); strncpy(then_b,ω,63); }

    if(elseb){
        char ep_γ[64]; char ep_ω[64]; strncpy(ep_γ,γ,63); strncpy(ep_ω,ω,63);
        emit_expr(elseb, ep_γ, ep_ω, else_a,else_b);
    } else { strncpy(else_a,ω,63); }

    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ,cond_then,63); strncpy(cp_ω,cond_else,63);
    char ca[64],cb[64];
    emit_expr(cond, cp_γ, cp_ω, ca,cb);

    /* cond_then: condition succeeded and pushed a value — discard it, enter then */
    Ldef(cond_then);
    E("    add     rsp, 8\n");  /* discard condition result value */
    Jmp(thenb?then_a:γ);
    /* cond_else: condition failed (no value pushed) — enter else */
    Ldef(cond_else); Jmp(elseb?else_a:ω);

    Ldef(a); Jmp(ca);
    Ldef(b); Jmp(cb);
}

/* =========================================================================
 * E_FNC — function call (write built-in OR user procedure)
 * ======================================================================= */
static void emit_call(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    if(n->nchildren<1){ emit_fail_node(n, γ, ω, oa, ob); return; }
    EXPR_t *fn=n->children[0];
    int nargs=n->nchildren-1;
    const char *fname=(fn->kind==E_VAR)?fn->sval:"unknown";
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    E("    ; CALL %s  id=%d\n",fname,id);

    /* --- built-in write --- */
    if(strcmp(fname,"write")==0){
        if(nargs==0){
            Ldef(a); E("    call    icn_write_str\n"); Jmp(γ);
            Ldef(b); Jmp(ω); return;
        }
        EXPR_t *arg=n->children[1];
        char after[64]; snprintf(after,sizeof after,"icon_%d_call",id);
        char ap2_γ[64]; char ap2_ω[64]; strncpy(ap2_γ,after,63); strncpy(ap2_ω,ω,63);
        char arg_a[64],arg_b[64];
        emit_expr(arg, ap2_γ, ap2_ω, arg_a,arg_b);
        Ldef(a); Jmp(arg_a);
        Ldef(b); Jmp(arg_b);
        Ldef(after);
        if(arg->kind==E_QLIT){
            /* emit_str sets rdi via lea; nothing on hw stack — just call */
            E("    call    icn_write_str\n");
        } else if(arg->kind==E_CSET){
            /* emit_cset sets rdi via lea; nothing on hw stack — just call */
            E("    call    icn_write_str\n");
        } else if(arg->kind==E_CAT || arg->kind==E_LCONCAT){
            /* emit_concat pushes result char* onto hw stack */
            E("    pop     rdi\n");
            E("    call    icn_write_str\n");
        } else {
            /* Everything else (VAR, CALL, INT, binop…) pushed a value.
             * Use type inference to pick the right runtime call. */
            E("    pop     rdi\n");
            char k = icn_expr_kind(arg);
            if (k == 'S')
                E("    call    icn_write_str\n");
            else
                E("    call    icn_write_int\n");
        }
        Jmp(γ);
        return;
    }

    /* --- scan builtins: any / many / upto --- */
    if((strcmp(fname,"any")==0||strcmp(fname,"many")==0||strcmp(fname,"upto")==0) && !icn_is_user_proc(fname)){
        const char *rtfn = strcmp(fname,"any")==0  ? "icn_any"  :
                           strcmp(fname,"many")==0 ? "icn_many" : "icn_upto";
        char after[64]; snprintf(after,sizeof after,"icon_%d_sbuiltin",id);
        if(nargs<1){
            Ldef(a); Jmp(ω);
            Ldef(b); Jmp(ω); return;
        }
        EXPR_t *arg=n->children[1];
        char ap2_γ[64]; char ap2_ω[64]; strncpy(ap2_γ,after,63); strncpy(ap2_ω,ω,63);
        char arg_a[64],arg_b[64];
        emit_expr(arg, ap2_γ, ap2_ω, arg_a,arg_b);
        Ldef(a); Jmp(arg_a);
        Ldef(b); Jmp(arg_b);
        Ldef(after);
        /* arg is cset/str: rdi already set; var/other: pop into rdi */
        if(arg->kind==E_CSET||arg->kind==E_QLIT){
            /* rdi already has the cset pointer from emit_str/emit_cset */
        } else {
            E("    pop     rdi\n");
        }
        E("    call    %s\n",rtfn);
        /* returns new pos (1-based int) or 0 on fail */
        E("    test    rax, rax\n");
        E("    jz      %s\n",ω);
        E("    push    rax\n");
        Jmp(γ);
        return;
    }

    /* --- match(s): one-shot — match s at current scan pos, return new 1-based pos --- */
    if(strcmp(fname,"match")==0 && !icn_is_user_proc(fname)){
        char after[64]; snprintf(after,sizeof after,"icon_%d_maft",id);
        if(nargs<1){ Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return; }
        EXPR_t *arg=n->children[1];
        char ap_γ[64]; char ap_ω[64]; strncpy(ap_γ,after,63); strncpy(ap_ω,ω,63);
        char aa[64],ab[64]; emit_expr(arg, ap_γ, ap_ω, aa,ab);
        Ldef(a); Jmp(aa);
        Ldef(b); Jmp(ω);
        Ldef(after);
        if(arg->kind==E_QLIT||arg->kind==E_CSET){ /* rdi already set */ }
        else { E("    pop     rdi\n"); }
        E("    call    icn_match\n");
        E("    test    rax, rax\n");
        E("    jz      %s\n",ω);
        E("    push    rax\n");
        Jmp(γ);
        return;
    }

    /* --- tab(n): one-shot — return subject[pos..n-1], set pos=n-1 (returns char*) --- */
    if(strcmp(fname,"tab")==0 && !icn_is_user_proc(fname)){
        char after[64]; snprintf(after,sizeof after,"icon_%d_taft",id);
        if(nargs<1){ Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return; }
        EXPR_t *arg=n->children[1];
        char ap_γ[64]; char ap_ω[64]; strncpy(ap_γ,after,63); strncpy(ap_ω,ω,63);
        char aa[64],ab[64]; emit_expr(arg, ap_γ, ap_ω, aa,ab);
        Ldef(a); Jmp(aa);
        Ldef(b); Jmp(ω);
        Ldef(after);
        E("    pop     rdi\n");   /* n (long) */
        E("    call    icn_tab\n");
        E("    test    rax, rax\n");
        E("    jz      %s\n",ω);
        E("    push    rax\n");   /* push char* result */
        Jmp(γ);
        return;
    }

    /* --- move(n): one-shot — return subject[pos..pos+n-1], advance pos by n (returns char*) --- */
    if(strcmp(fname,"move")==0 && !icn_is_user_proc(fname)){
        char after[64]; snprintf(after,sizeof after,"icon_%d_mvaft",id);
        if(nargs<1){ Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return; }
        EXPR_t *arg=n->children[1];
        char ap_γ[64]; char ap_ω[64]; strncpy(ap_γ,after,63); strncpy(ap_ω,ω,63);
        char aa[64],ab[64]; emit_expr(arg, ap_γ, ap_ω, aa,ab);
        Ldef(a); Jmp(aa);
        Ldef(b); Jmp(ω);
        Ldef(after);
        E("    pop     rdi\n");   /* n (long) */
        E("    call    icn_move\n");
        E("    test    rax, rax\n");
        E("    jz      %s\n",ω);
        E("    push    rax\n");   /* push char* result */
        Jmp(γ);
        return;
    }

    /* --- find(s1, s2): generator — successive 1-based positions of s1 in s2 ---
     * State: icn_find_s1_N (char* BSS), icn_find_s2_N (char* BSS),
     *        icn_find_pos_N (long BSS, next 0-based search offset).
     * α: evaluate args, init pos=0, enter check.
     * β: pos = last_result (1-based), re-enter check.
     * check: call icn_str_find(s1,s2,pos); 0→ω, else store result as new pos, push→γ. */
    if(strcmp(fname,"find")==0 && nargs>=2 && !icn_is_user_proc(fname)){
        char s1bss[80],s2bss[80],posbss[80];
        snprintf(s1bss,  sizeof s1bss,  "icn_find_s1_%d",  id);
        snprintf(s2bss,  sizeof s2bss,  "icn_find_s2_%d",  id);
        snprintf(posbss, sizeof posbss, "icn_find_pos_%d", id);
        bss_declare(s1bss); bss_declare(s2bss); bss_declare(posbss);

        char after1[64],after2[64],chk[64];
        snprintf(after1,sizeof after1,"icon_%d_fa1",id);
        snprintf(after2,sizeof after2,"icon_%d_fa2",id);
        snprintf(chk,   sizeof chk,   "icon_%d_fchk",id);

        EXPR_t *s1arg=n->children[1], *s2arg=n->children[2];
        char ap1_γ[64]; char ap1_ω[64]; strncpy(ap1_γ,after1,63); strncpy(ap1_ω,ω,63);
        char a1[64],b1[64]; emit_expr(s1arg, ap1_γ, ap1_ω, a1,b1);
        char ap2_γ[64]; char ap2_ω[64]; strncpy(ap2_γ,after2,63); strncpy(ap2_ω,ω,63);
        char a2[64],b2[64]; emit_expr(s2arg, ap2_γ, ap2_ω, a2,b2);

        /* α: eval s1 → store, eval s2 → store, init pos=0, check */
        Ldef(a); Jmp(a1);
        Ldef(after1);
        if(s1arg->kind==E_QLIT||s1arg->kind==E_CSET)
            E("    mov     [rel %s], rdi\n",s1bss);
        else { E("    pop     rax\n"); E("    mov     [rel %s], rax\n",s1bss); }
        Jmp(a2);
        Ldef(after2);
        if(s2arg->kind==E_QLIT||s2arg->kind==E_CSET)
            E("    mov     [rel %s], rdi\n",s2bss);
        else { E("    pop     rax\n"); E("    mov     [rel %s], rax\n",s2bss); }
        E("    mov     qword [rel %s], 0\n",posbss);
        Jmp(chk);

        /* β: last result was 1-based; next search starts at that same index (0-based=result) */
        Ldef(b);
        /* pos is already stored as last 1-based result → use as next 0-based from */
        Jmp(chk);

        /* check */
        Ldef(chk);
        E("    mov     rdi, [rel %s]\n",s1bss);
        E("    mov     rsi, [rel %s]\n",s2bss);
        E("    mov     rdx, [rel %s]\n",posbss);
        E("    call    icn_str_find\n");
        E("    test    rax, rax\n");
        E("    jz      %s\n",ω);
        /* store result as new pos (next β will search from result, i.e. 0-based=result) */
        E("    mov     [rel %s], rax\n",posbss);
        E("    push    rax\n");
        Jmp(γ);
        return;
    }

    /* --- user procedure call --- */
    if(icn_is_user_proc(fname)){
        int is_gen = icn_is_gen_proc(fname);
        char do_call[64]; snprintf(do_call,sizeof do_call,"icon_%d_docall",id);

        char (*arg_alphas)[64] = nargs>0 ? malloc(nargs*64) : NULL;
        char (*arg_betas) [64] = nargs>0 ? malloc(nargs*64) : NULL;

        char prev_succ[64]; strncpy(prev_succ,do_call,63);

        for(int i=nargs-1;i>=0;i--){
            char push_relay[64]; snprintf(push_relay,sizeof push_relay,"icon_%d_push%d",id,i);
            char ap3_γ[64]; char ap3_ω[64]; strncpy(ap3_γ,push_relay,63); strncpy(ap3_ω,ω,63);
            emit_expr(n->children[i+1], ap3_γ, ap3_ω, arg_alphas[i], arg_betas[i]);
            Ldef(push_relay);
            E("    pop     rdi\n");
            E("    call    icn_push\n");
            Jmp(prev_succ);
            strncpy(prev_succ,arg_alphas[i],63);
        }

        Ldef(a);
        if(nargs>0) Jmp(prev_succ);
        else Jmp(do_call);

        /* β: for generators resume via suspend slot; for normal procs just fail */
        Ldef(b);
        if(is_gen){
            E("    ; call β — resume if suspended, fail otherwise\n");
            E("    movzx   rax, byte [rel icn_suspended]\n");
            E("    test    rax, rax\n");
            E("    jz      %s\n", ω);
            E("    mov     byte [rel icn_suspended], 0\n");
            E("    mov     rbp, [rel icn_suspend_rbp]\n");
            E("    jmp     [rel icn_suspend_resume]\n");
        } else {
            Jmp(ω);
        }

        /* do_call: all args on icn_stack */
        Ldef(do_call);
        if(is_gen){
            /* jmp-based trampoline — frame stays live across suspend/resume */
            char after_call[64]; snprintf(after_call,sizeof after_call,"icon_%d_after_call",id);
            char caller_ret[80]; snprintf(caller_ret,sizeof caller_ret,"icn_u_%s_caller_ret",fname);
            E("    mov     byte [rel icn_suspended], 0\n");
            E("    lea     rax, [rel %s]\n", after_call);
            E("    mov     [rel %s], rax\n", caller_ret);
            E("    jmp     icn_u_%s\n", fname);
            Ldef(after_call);
            E("    movzx   rax, byte [rel icn_failed]\n");
            E("    test    rax, rax\n");
            E("    jnz     %s\n",ω);
            E("    movzx   rax, byte [rel icn_suspended]\n");
            E("    test    rax, rax\n");
            char did_return[64]; snprintf(did_return,sizeof did_return,"icon_%d_returned",id);
            E("    jz      %s\n",did_return);
            E("    mov     rax, [rel icn_retval]\n");
            E("    push    rax\n");
            Jmp(γ);
            Ldef(did_return);
            E("    mov     rax, [rel icn_retval]\n");
            E("    push    rax\n");
            Jmp(γ);
        } else {
            /* Normal call/ret — safe for recursion */
            E("    call    icn_u_%s\n",fname);
            E("    movzx   rax, byte [rel icn_failed]\n");
            E("    test    rax, rax\n");
            E("    jnz     %s\n",ω);
            E("    mov     rax, [rel icn_retval]\n");
            E("    push    rax\n");
            Jmp(γ);
        }

        if(arg_alphas) free(arg_alphas);
        if(arg_betas)  free(arg_betas);
        return;
    }

    /* Unknown call — just fail */
    Ldef(a); Jmp(ω);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * ICN_ALT — value alternation E1 | E2 | ... | En  (n-ary flat array)
 * α → E1.α; E1_ω → E2.α; ... ; En_ω → node_ω
 * β → E1.β (resume leftmost; irgen.icn simple alternation model)
 * ======================================================================= */
static void emit_alt(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    int nc = n->nchildren;
    /* Allocate α/β label storage for each child */
    char (*ca)[64] = malloc(nc * 64);
    char (*cb)[64] = malloc(nc * 64);

    /* Emit right-to-left so ω of child[i] → α of child[i+1] */
    for (int i = nc - 1; i >= 0; i--) {
        char ep_γ[64]; char ep_ω[64];
        strncpy(ep_γ, γ, 63);
        strncpy(ep_ω, (i == nc-1) ? ω : ca[i+1], 63);
        emit_expr(n->children[i], ep_γ, ep_ω, ca[i], cb[i]);
    }

    Ldef(a); Jmp(ca[0]);
    Ldef(b); Jmp(cb[0]);
    free(ca); free(cb);
}

/* =========================================================================
 * E_CSET — cset literal (single-quoted string)
 * Treated identically to E_QLIT for pointer passing: lea rdi, [rel label]
 * ======================================================================= */
static void emit_cset(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    emit_str(n, γ, ω, oa, ob);   /* same layout — char* in rdi, nothing pushed */
}

/* =========================================================================
 * ICN_RANDOM — ?E: random integer in 1..E
 * Evaluate E, call icn_random(n), push result. Fails if E fails.
 * ======================================================================= */
static void emit_random(EXPR_t *n, const char *γ, const char *ω,
                        char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char after[64]; snprintf(after, sizeof after, "icon_%d_rand", id);
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ, after, 63); strncpy(cp_ω, ω, 63);
    char ca[64], cb[64];
    emit_expr(n->children[0], cp_γ, cp_ω, ca, cb);
    Ldef( a); Jmp( ca);
    Ldef( b); Jmp( cb);
    Ldef( after);
    E( "    pop     rdi\n");
    E( "    extern  icn_random\n");
    E( "    call    icn_random\n");
    E( "    push    rax\n");
    Jmp( γ);
}

/* =========================================================================
 * ICN_NEG — unary minus: -E
 * Evaluate E, negate result, push, succeed. One-shot (no β retry).
 * ======================================================================= */
static void emit_neg(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char after[64]; snprintf(after, sizeof after, "icon_%d_neg", id);
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ, after, 63); strncpy(cp_ω, ω, 63);
    char ca[64], cb[64];
    emit_expr(n->children[0], cp_γ, cp_ω, ca, cb);
    Ldef( a); Jmp( ca);
    Ldef( b); Jmp( cb);
    Ldef( after);
    E( "    pop     rax\n");
    E( "    neg     rax\n");
    E( "    push    rax\n");
    Jmp( γ);
}

/* =========================================================================
 * emit_not  --  /E: succeed iff E fails
 * ======================================================================= */
static void emit_not(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char e_ok[64];   snprintf(e_ok,   sizeof e_ok,   "icon_%d_eok",   id);
    char e_fail[64]; snprintf(e_fail, sizeof e_fail, "icon_%d_efail", id);
    char cp_γ[64]; char cp_ω[64];
    strncpy(cp_γ, e_ok,   63);
    strncpy(cp_ω, e_fail, 63);
    char ca[64], cb[64];
    emit_expr(n->children[0], cp_γ, cp_ω, ca, cb);
    Ldef( a); Jmp( ca);
    Ldef( b); Jmp( ω);
    Ldef( e_ok);
    E( "    add     rsp, 8\n");
    Jmp( ω);
    Ldef( e_fail);
    E( "    push    0\n");
    Jmp( γ);
}

/* =========================================================================
 * emit_seq  --  string equality E1 == E2
 * ======================================================================= */
static void emit_seq(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    char chk[64];         snprintf(chk,        sizeof chk,        "icon_%d_check",  id);
    char lbfwd[64];       snprintf(lbfwd,       sizeof lbfwd,      "icon_%d_lb",     id);
    char lstore[64];      snprintf(lstore,      sizeof lstore,     "icon_%d_lstore", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay, "icon_%d_lrelay", id);
    char right_relay[64]; snprintf(right_relay, sizeof right_relay,"icon_%d_rrelay", id);
    char lc_ptr[64];      snprintf(lc_ptr,      sizeof lc_ptr,     "icn_sq%d_lptr",  id);
    char rc_ptr[64];      snprintf(rc_ptr,      sizeof rc_ptr,     "icn_sq%d_rptr",  id);
    bss_declare(lc_ptr); bss_declare(rc_ptr);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    E( "    ; SEQ  id=%d\n", id);
    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ, right_relay, 63); strncpy(rp_ω, lbfwd, 63);
    char ra[64], rb[64]; emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ, left_relay,  63); strncpy(lp_ω, ω, 63);
    char la[64], lb2[64]; emit_expr(n->children[0], lp_γ, lp_ω, la, lb2);
    int lstr=(n->children[0]->kind==E_QLIT||n->children[0]->kind==E_CSET);
    int rstr=(n->children[1]->kind==E_QLIT||n->children[1]->kind==E_CSET);
    Ldef(left_relay);
    if(lstr){E("    mov     [rel %s], rdi\n",lc_ptr);}
    else    {E("    pop     rax\n");E("    mov     [rel %s], rax\n",lc_ptr);}
    Jmp(lstore);
    Ldef(right_relay);
    if(rstr){E("    mov     [rel %s], rdi\n",rc_ptr);}
    else    {E("    pop     rax\n");E("    mov     [rel %s], rax\n",rc_ptr);}
    Jmp(chk);
    Ldef(lbfwd);  Jmp(lb2);
    Ldef(a);      Jmp(la);
    Ldef(b);      Jmp(rb);
    Ldef(lstore); Jmp(ra);
    Ldef(chk);
    E("    mov     rdi, [rel %s]\n",lc_ptr);
    E("    mov     rsi, [rel %s]\n",rc_ptr);
    E("    call    icn_str_eq\n");
    E("    test    rax, rax\n");
    E("    jz      %s\n",rb);
    E("    push    0\n");
    Jmp(γ);
}

static void emit_concat(EXPR_t *n, const char *γ, const char *ω,
                        char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    char compute[64];     snprintf(compute,     sizeof compute,     "icon_%d_compute", id);
    char lbfwd[64];       snprintf(lbfwd,       sizeof lbfwd,       "icon_%d_lb",      id);
    char lstore[64];      snprintf(lstore,      sizeof lstore,      "icon_%d_lstore",  id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icon_%d_lrelay",  id);
    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icon_%d_rrelay",  id);
    char lc_ptr[64];      snprintf(lc_ptr,      sizeof lc_ptr,      "icn_cc%d_lptr",   id);
    char rc_ptr[64];      snprintf(rc_ptr,      sizeof rc_ptr,      "icn_cc%d_rptr",   id);
    bss_declare(lc_ptr);
    bss_declare(rc_ptr);
    int bf_slot = locals_alloc_tmp();
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    E( "    ; CONCAT  id=%d\n", id);

    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ, right_relay, 63); strncpy(rp_ω, lbfwd, 63);
    char ra[64], rb[64];
    emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);

    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ, left_relay, 63); strncpy(lp_ω, ω, 63);
    char la[64], lb[64];
    emit_expr(n->children[0], lp_γ, lp_ω, la, lb);

    EXPR_t *lch = n->children[0];
    EXPR_t *rch = n->children[1];
    int left_is_value = (lch->kind == E_VAR || lch->kind == E_QLIT || lch->kind == E_FNC);
    int left_is_str   = (lch->kind == E_QLIT);
    int right_is_str  = (rch->kind == E_QLIT);

    /* left_relay: left succeeded → normalise to pointer in lc_ptr */
    Ldef( left_relay);
    if (left_is_str) {
        /* emit_str put pointer in rdi, nothing on hw stack */
        E( "    mov     [rel %s], rdi\n", lc_ptr);
    } else {
        /* emit_var/emit_call pushed value onto hw stack */
        E( "    pop     rax\n");
        E( "    mov     [rel %s], rax\n", lc_ptr);
    }
    Jmp( lstore);

    /* right_relay: right succeeded → normalise to pointer in rc_ptr */
    Ldef( right_relay);
    if (right_is_str) {
        E( "    mov     [rel %s], rdi\n", rc_ptr);
    } else {
        E( "    pop     rax\n");
        E( "    mov     [rel %s], rax\n", rc_ptr);
    }
    Jmp( compute);

    /* lbfwd: right exhausted → retry left */
    Ldef( lbfwd); Jmp( lb);

    /* node α: fresh start */
    Ldef( a);
    E( "    mov     qword [rbp%+d], 0\n", slot_offset(bf_slot));
    Jmp( la);

    /* node β: resume */
    Ldef( b);
    if (left_is_value) {
        E( "    mov     qword [rbp%+d], 1\n", slot_offset(bf_slot));
        Jmp( la);
    } else {
        Jmp( rb);
    }

    /* lstore: left cached → start (α) or resume (β) right */
    Ldef( lstore);
    E( "    cmp     qword [rbp%+d], 0\n", slot_offset(bf_slot));
    E( "    je      %s\n", ra);
    Jmp( rb);

    /* compute: call icn_str_concat(lc_ptr, rc_ptr) → push result char* */
    Ldef( compute);
    E( "    mov     rdi, [rel %s]\n", lc_ptr);
    E( "    mov     rsi, [rel %s]\n", rc_ptr);
    E( "    call    icn_str_concat\n");
    E( "    push    rax\n");
    Jmp( γ);
}

/* =========================================================================
 * ICN_SCAN — E ? body
 *
 * Byrd-box wiring (following JVM ij_emit_scan):
 *   α → expr.α
 *   expr_γ (new subject — char* on hw stack or rdi for E_QLIT):
 *     save old icn_subject → old_subj_N
 *     save old icn_pos    → old_pos_N
 *     install new subject into icn_subject, reset icn_pos=0
 *     → body.α
 *   body_γ → restore old subject/pos → γ
 *   body_ω → restore old subject/pos → expr.β → ω (one-shot)
 *   β → restore → body.β
 *
 * &subject keyword is handled in emit_var: loads icn_subject.
 * ======================================================================= */
static void emit_scan(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    /* Global subject/pos */
    bss_declare("icn_subject");
    bss_declare("icn_pos");

    /* Per-scan save slots */
    char old_subj[64], old_pos[64];
    snprintf(old_subj, sizeof old_subj, "icn_scan_oldsubj_%d", id);
    snprintf(old_pos,  sizeof old_pos,  "icn_scan_oldpos_%d",  id);
    bss_declare(old_subj);
    bss_declare(old_pos);

    char setup[64], body_ok[64], body_fail[64], beta_restore[64];
    snprintf(setup,        sizeof setup,        "icon_%d_scan_setup", id);
    snprintf(body_ok,      sizeof body_ok,      "icon_%d_scan_bok",   id);
    snprintf(body_fail,    sizeof body_fail,     "icon_%d_scan_bfail", id);
    snprintf(beta_restore, sizeof beta_restore,  "icon_%d_scan_bret",  id);

    EXPR_t *expr_node = (n->nchildren >= 1) ? n->children[0] : NULL;
    EXPR_t *body_node = (n->nchildren >= 2) ? n->children[1] : NULL;

    /* Wire expr: γ → setup, ω → ω */
    char ep_γ[64]; char ep_ω[64]; strncpy(ep_γ, setup, 63); strncpy(ep_ω, ω, 63);
    char ea[64], eb[64];
    emit_expr(expr_node, ep_γ, ep_ω, ea, eb);

    /* Wire body: γ → body_ok, ω → body_fail */
    char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ, body_ok, 63); strncpy(bp_ω, body_fail, 63);
    char ba[64], bb[64];
    if (body_node)
        emit_expr(body_node, bp_γ, bp_ω, ba, bb);
    else {
        strncpy(ba, body_ok, 63); strncpy(bb, body_fail, 63);
    }

    int expr_is_str = (expr_node && expr_node->kind == E_QLIT);

    /* α → expr.α */
    E( "    ; SCAN  id=%d\n", id);
    Ldef( a); Jmp( ea);

    /* β → restore + body.β */
    Ldef( b); Jmp( beta_restore);

    /* setup: expr succeeded — new subject pointer in rdi (E_QLIT) or on hw stack */
    Ldef( setup);
    if (expr_is_str) {
        E( "    ; scan setup: subject in rdi\n");
        /* save old subject */
        E( "    mov     rax, [rel icn_subject]\n");
        E( "    mov     [rel %s], rax\n", old_subj);
        /* save old pos */
        E( "    mov     rax, [rel icn_pos]\n");
        E( "    mov     [rel %s], rax\n", old_pos);
        /* install new subject from rdi */
        E( "    mov     [rel icn_subject], rdi\n");
    } else {
        /* new subject pointer on hw stack */
        E( "    pop     rax\n");
        /* save old subject */
        E( "    push    rax\n");   /* keep new on stack while saving old */
        E( "    mov     rcx, [rel icn_subject]\n");
        E( "    mov     [rel %s], rcx\n", old_subj);
        /* save old pos */
        E( "    mov     rcx, [rel icn_pos]\n");
        E( "    mov     [rel %s], rcx\n", old_pos);
        /* install new subject */
        E( "    pop     rax\n");
        E( "    mov     [rel icn_subject], rax\n");
    }
    /* reset pos to 0 */
    E( "    mov     qword [rel icn_pos], 0\n");
    Jmp( ba);

    /* body_γ — success: restore, → γ */
    Ldef( body_ok);
    E( "    mov     rax, [rel %s]\n", old_subj);
    E( "    mov     [rel icn_subject], rax\n");
    E( "    mov     rax, [rel %s]\n", old_pos);
    E( "    mov     [rel icn_pos], rax\n");
    Jmp( γ);

    /* body_ω — fail: restore, → expr.β → ω */
    Ldef( body_fail);
    E( "    mov     rax, [rel %s]\n", old_subj);
    E( "    mov     [rel icn_subject], rax\n");
    E( "    mov     rax, [rel %s]\n", old_pos);
    E( "    mov     [rel icn_pos], rax\n");
    Jmp( eb);

    /* beta_restore: node β — restore then body.β */
    Ldef( beta_restore);
    E( "    mov     rax, [rel %s]\n", old_subj);
    E( "    mov     [rel icn_subject], rax\n");
    E( "    mov     rax, [rel %s]\n", old_pos);
    E( "    mov     [rel icn_pos], rax\n");
    Jmp( bb);
}

/* =========================================================================
 * Binary arithmetic — funcs-set wiring (§4.3)
 * Frame slot lc_slot: left cache (per-invocation, prevents recursive clobber).
 * BSS bflag: 0=α path (start right from α), 1=β path (resume right from β).
 *   α: bflag=0, run left → lstore → cache → ra
 *   β: bflag=1, run left → lstore → cache → rb
 * right_ω → lbfwd → left.β → binop fails
 * ======================================================================= */
static void emit_binop(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64],compute[64],lbfwd[64],lstore[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(compute,sizeof compute,"icon_%d_compute",id);
    snprintf(lbfwd,  sizeof lbfwd,  "icon_%d_lb",id);
    snprintf(lstore, sizeof lstore, "icon_%d_lstore",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *op=n->kind==E_ADD?"ADD":n->kind==E_SUB?"SUB":n->kind==E_MUL?"MUL":n->kind==E_DIV?"DIV":"MOD";
    E("    ; %s  id=%d\n",op,id);

    int lc_slot = locals_alloc_tmp();
    int bf_slot = locals_alloc_tmp();

    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ,compute,63); strncpy(rp_ω,lbfwd,63);
    char ra[64],rb[64]; emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);

    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ,lstore,63); strncpy(lp_ω,ω,63);
    char la[64],lb[64]; emit_expr(n->children[0], lp_γ, lp_ω, la, lb);

    /* Heuristic: if left is a simple value (var/int/str/call), β must re-eval
     * left to refresh it (e.g. updated `total`). If left is a generator
     * (TO, binop, relop, etc.), β goes directly to right.β — left cache
     * is still valid and re-running left would reset the generator. */
    EXPR_t *left_child = n->children[0];
    int left_is_value = (left_child->kind == E_VAR || left_child->kind == E_ILIT ||
                         left_child->kind == E_QLIT || left_child->kind == E_FNC);

    Ldef(lbfwd); Jmp(lb);
    Ldef(a);
    E("    mov     qword [rbp%+d], 0\n", slot_offset(bf_slot));  /* α: start right */
    Jmp(la);
    Ldef(b);
    if (left_is_value) {
        /* value-left: β re-evals left to refresh (e.g. accumulated total) */
        E("    mov     qword [rbp%+d], 1\n", slot_offset(bf_slot));
        Jmp(la);
    } else {
        /* generator-left: β goes directly to right.β — left cache still valid */
        Jmp(rb);
    }

    /* lstore: cache left, branch on bf_slot → start right (α) or resume right (β) */
    Ldef(lstore);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(lc_slot));
    E("    cmp     qword [rbp%+d], 0\n", slot_offset(bf_slot));
    E("    je      %s\n", ra);
    Jmp(rb);

    Ldef(compute);
    E("    pop     rax\n");
    E("    mov     rcx, [rbp%+d]\n", slot_offset(lc_slot));
    switch(n->kind){
        case E_ADD: E("    add     rcx, rax\n"); break;
        case E_SUB: E("    sub     rcx, rax\n"); break;
        case E_MUL: E("    imul    rcx, rax\n"); break;
        case E_DIV: E("    xchg    rax, rcx\n    cqo\n    idiv    rcx\n    mov     rcx, rax\n"); break;
        case E_MOD: E("    xchg    rax, rcx\n    cqo\n    idiv    rcx\n    mov     rcx, rdx\n"); break;
        default: break;
    }
    E("    push    rcx\n");
    Jmp(γ);
}

/* =========================================================================
 * Relational operators — goal-directed retry
 * Same left-cache pattern as binop.
 * On comparison failure → right.β (retry right, left still in lcache).
 * On right exhausted → left.β (retry left, re-stores lcache).
 * ======================================================================= */
static void emit_relop(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64],chk[64],lbfwd[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(chk,       sizeof chk,       "icon_%d_check",id);
    snprintf(lbfwd,     sizeof lbfwd,     "icon_%d_lb",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *op=n->kind==E_LT?"LT":n->kind==E_LE?"LE":n->kind==E_GT?"GT":n->kind==E_GE?"GE":n->kind==E_EQ?"EQ":"NE";
    E("    ; %s  id=%d\n",op,id);

    char lcache_store[64]; snprintf(lcache_store,sizeof lcache_store,"icon_%d_lstore",id);
    int lc_slot = locals_alloc_tmp();

    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ,chk,63); strncpy(rp_ω,lbfwd,63);
    char ra[64],rb[64]; emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ,lcache_store,63); strncpy(lp_ω,ω,63);
    char la[64],lb[64]; emit_expr(n->children[0], lp_γ, lp_ω, la, lb);

    Ldef(lbfwd); Jmp(lb);
    Ldef(a); Jmp(la);
    Ldef(b); Jmp(rb);

    Ldef(lcache_store);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(lc_slot));
    Jmp(ra);

    Ldef(chk);
    E("    pop     rcx\n");                                    /* right value */
    E("    mov     rax, [rbp%+d]\n", slot_offset(lc_slot));   /* left from frame */
    E("    cmp     rax, rcx\n");
    const char *jfail=n->kind==E_LT?"jge":n->kind==E_LE?"jg":n->kind==E_GT?"jle":n->kind==E_GE?"jl":n->kind==E_EQ?"jne":"je";
    E("    %s      %s\n",jfail,rb);
    E("    push    rcx\n");
    Jmp(γ);
}

/* =========================================================================
 * ICN_TO — range generator inline counter (§4.4)
 * E1 and E2 each push their value onto the stack.
 * init: pop E2 value → BSS e2_bound; pop E1 value → BSS I (counter start)
 * β:   inc I, re-check
 * BSS I and e2_bound are per-TO-node but NOT per-invocation — safe here
 * because TO is only used inside `every` (flat, non-recursive context).
 * ======================================================================= */
static void emit_to(EXPR_t *n, const char *γ, const char *ω,
                    char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64],code[64],init[64],e1bf[64],e2bf[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    icn_lbl_code(id,code,sizeof code);
    snprintf(init,sizeof init,"icon_%d_init",id);
    snprintf(e1bf,sizeof e1bf,"icon_%d_e1b",id);
    snprintf(e2bf,sizeof e2bf,"icon_%d_e2b",id);
    char I[64],bound[64]; label_I(id,I,sizeof I);
    snprintf(bound,sizeof bound,"icon_%d_bound",id);
    bss_declare(I); bss_declare(bound);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; TO  id=%d\n",id);

    /* e1cur: current E1 value (updated each time E1 succeeds).
     * e2_seen: 0 on first init (E1+E2 both on stack), 1 thereafter (only E2). */
    char e1cur[64];  snprintf(e1cur,  sizeof e1cur,  "icon_%d_e1cur",id);
    char e2seen[64]; snprintf(e2seen, sizeof e2seen, "icon_%d_e2seen",id);
    bss_declare(e1cur); bss_declare(e2seen);

    char e2p_γ[64]; char e2p_ω[64]; strncpy(e2p_γ,init,63); strncpy(e2p_ω,e1bf,63);
    char e2a[64],e2b[64]; emit_expr(n->children[1], e2p_γ, e2p_ω, e2a, e2b);
    char e1p_γ[64]; char e1p_ω[64]; strncpy(e1p_γ,e2a,63); strncpy(e1p_ω,ω,63);
    char e1a[64],e1b[64]; emit_expr(n->children[0], e1p_γ, e1p_ω, e1a, e1b);

    /* e1bf: E1 advancing → reset e2_seen so next init pops both from stack */
    Ldef(e1bf);
    E("    mov     qword [rel %s], 0\n",e2seen);
    Jmp(e1b);
    Ldef(e2bf); Jmp(e2b);
    Ldef(a);
    E("    mov     qword [rel %s], 0\n",e2seen);   /* α: mark fresh start */
    Jmp(e1a);
    Ldef(b); E("    inc     qword [rel %s]\n",I); Jmp(code);

    /* init: E2 just pushed its value on top of stack.
     * e2_seen==0: E1 also on stack (below) — first time.
     * e2_seen==1: only E2 on stack — E2 advanced, reset I to current E1 value. */
    Ldef(init);
    E("    pop     rax\n");                          /* E2 value (always on top) */
    E("    mov     [rel %s], rax\n",bound);
    E("    cmp     qword [rel %s], 0\n",e2seen);
    E("    jne     %s_e2adv\n",init);
    /* first time: pop E1 value, save as e1cur, set I */
    E("    pop     rax\n");
    E("    mov     [rel %s], rax\n",e1cur);
    E("    mov     [rel %s], rax\n",I);
    E("    mov     qword [rel %s], 1\n",e2seen);
    Jmp(code);
    /* E2 advanced: reset I to current E1 value (not first E1 value) */
    E("%s_e2adv:\n",init);
    E("    mov     rax, [rel %s]\n",e1cur);
    E("    mov     [rel %s], rax\n",I);
    Jmp(code);

    /* When E1 advances (e1b fires after e2 exhausts), update e1cur and reset e2_seen */
    /* We intercept by patching e1b's flow: e1b → e1cur_update → original e1b path.
     * But e1b is already emitted inside emit_expr. Instead, emit a wrapper:
     * e1bf already jumps to e1b. When E1 succeeds again (e1p_γ = e2a),
     * E1 pushes new value and jumps to e2a (E2 start). E2's succeed = init.
     * At that point e2_seen=1 → e2adv path which uses e1cur.
     * But e1cur still has the OLD E1 value! We need to update e1cur when E1 succeeds.
     * Solution: intercept E1's succeed: e1p_γ → e1_capture → e2a */
    /* NOTE: e1p was already emitted above with succeed=e2a. We can't change that.
     * Instead: e2_seen reset to 0 when E1 advances (e1bf fires).
     * That way next init will pop E1 from stack and update e1cur correctly. */

    Ldef(code);
    E("    mov     rax, [rel %s]\n",I);
    E("    cmp     rax, [rel %s]\n",bound);
    E("    jg      %s\n",e2bf);
    E("    push    rax\n");              /* push current counter value */
    Jmp(γ);
}

/* =========================================================================
 * ICN_TO_BY
 * ======================================================================= */
static void emit_to_by(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64],code[64],init[64],e1bf[64],e2bf[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    icn_lbl_code(id,code,sizeof code);
    snprintf(init, sizeof init, "icon_%d_init", id);
    snprintf(e1bf, sizeof e1bf, "icon_%d_e1b",  id);
    snprintf(e2bf, sizeof e2bf, "icon_%d_e2b",  id);
    /* BSS slots for I (counter), bound, step */
    char I[64], bound[64], step[64];
    label_I(id,I,sizeof I);
    snprintf(bound, sizeof bound, "icon_%d_bound", id);
    snprintf(step,  sizeof step,  "icon_%d_step",  id);
    bss_declare(I); bss_declare(bound); bss_declare(step);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; TO_BY  id=%d\n",id);

    /* Wire E3 (step): succeed → init, fail → e2bf */
    char step_relay[64]; snprintf(step_relay,sizeof step_relay,"icon_%d_stepr",id);
    char e3p_γ[64]; char e3p_ω[64]; strncpy(e3p_γ,step_relay,63); strncpy(e3p_ω,e2bf,63);
    char e3a[64],e3b[64]; emit_expr(n->children[2], e3p_γ, e3p_ω, e3a, e3b);

    /* Wire E2 (bound): succeed → e3a, fail → e1bf */
    char bound_relay[64]; snprintf(bound_relay,sizeof bound_relay,"icon_%d_boundr",id);
    char e2p_γ[64]; char e2p_ω[64]; strncpy(e2p_γ,bound_relay,63); strncpy(e2p_ω,e1bf,63);
    char e2a[64],e2b[64]; emit_expr(n->children[1], e2p_γ, e2p_ω, e2a, e2b);

    /* Wire E1 (start): succeed → e2a, fail → ω */
    char start_relay[64]; snprintf(start_relay,sizeof start_relay,"icon_%d_startr",id);
    char e1p_γ[64]; char e1p_ω[64]; strncpy(e1p_γ,start_relay,63); strncpy(e1p_ω,ω,63);
    char e1a[64],e1b[64]; emit_expr(n->children[0], e1p_γ, e1p_ω, e1a, e1b);

    /* Relay: E1 pushed start → pop into I */
    Ldef(start_relay);
    E("    pop     rax\n");
    E("    mov     [rel %s], rax\n", I);
    Jmp(e2a);

    /* Relay: E2 pushed bound → pop into bound slot */
    Ldef(bound_relay);
    E("    pop     rax\n");
    E("    mov     [rel %s], rax\n", bound);
    Jmp(e3a);

    /* Relay: E3 pushed step → pop into step slot → proceed to code */
    Ldef(step_relay);
    E("    pop     rax\n");
    E("    mov     [rel %s], rax\n", step);
    Jmp(code);

    Ldef(e1bf); Jmp(e1b);
    Ldef(e2bf); Jmp(e2b);

    /* α: start from E1 */
    Ldef(a); Jmp(e1a);

    /* β: advance I by step, re-check */
    Ldef(b);
    E("    mov     rax, [rel %s]\n", step);
    E("    add     [rel %s], rax\n", I);
    Jmp(code);

    /* init label kept for compatibility — not used in this design */
    Ldef(init); Jmp(code);

    /* code: check I <= bound, push I, succeed */
    Ldef(code);
    E("    mov     rax, [rel %s]\n", I);
    E("    cmp     rax, [rel %s]\n", bound);
    E("    jg      %s\n", e2bf);
    E("    push    rax\n");
    Jmp(γ);
}

/* =========================================================================
 * ICN_WHILE — while cond do body
 * α → cond.α
 * cond_γ → body.α  (discard cond value)
 * cond_ω → while_ω  (condition failed → loop done)
 * body_γ → cond.α  (loop back regardless of body result)
 * body_ω → cond.α  (loop back)
 * ======================================================================= */
static void emit_while(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; WHILE  id=%d\n",id);

    EXPR_t *cond = n->children[0];
    EXPR_t *body = (n->nchildren>1) ? n->children[1] : NULL;

    char cond_ok[64]; snprintf(cond_ok,sizeof cond_ok,"icon_%d_condok",id);
    char loop_top[64]; snprintf(loop_top,sizeof loop_top,"icon_%d_top",id);

    char ca[64],cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ,cond_ok,63); strncpy(cp_ω,ω,63);
    emit_expr(cond, cp_γ, cp_ω, ca,cb);

    /* cond_ok: condition succeeded, value on stack — discard it, run body */
    Ldef(cond_ok);
    E("    add     rsp, 8\n");   /* discard condition result */

    if(body){
        char ba[64],bb[64];
        char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ,loop_top,63); strncpy(bp_ω,loop_top,63);
        loop_push(ω, loop_top);
        emit_expr(body, bp_γ, bp_ω, ba,bb);
        loop_pop();
        Jmp(ba);

        /* loop_top: go back to condition */
        Ldef(loop_top); Jmp(ca);
    } else {
        Jmp(ca);
    }

    Ldef(a); Jmp(ca);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * ICN_UNTIL — until cond do body
 * α → cond.α
 * cond_γ → discard value → ω  (cond succeeded → done)
 * cond_ω → body.α                   (cond failed → run body)
 * body_γ → cond.α  (loop back)
 * body_ω → cond.α  (loop back)
 * ======================================================================= */
static void emit_until(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; UNTIL  id=%d\n",id);

    EXPR_t *cond = n->children[0];
    EXPR_t *body = (n->nchildren>1) ? n->children[1] : NULL;

    char cond_ok[64];   snprintf(cond_ok,  sizeof cond_ok,  "icon_%d_condok", id);
    char cond_fail[64]; snprintf(cond_fail,sizeof cond_fail,"icon_%d_cfail",  id);
    char loop_top[64];  snprintf(loop_top, sizeof loop_top, "icon_%d_top",    id);

    char ca[64],cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ,cond_ok,63); strncpy(cp_ω,cond_fail,63);
    emit_expr(cond, cp_γ, cp_ω, ca,cb);

    /* cond succeeded → discard value, exit */
    Ldef(cond_ok);
    E("    add     rsp, 8\n");   /* discard condition result */
    Jmp(ω);

    /* cond failed → run body */
    Ldef(cond_fail);
    if(body){
        char ba[64],bb[64];
        char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ,loop_top,63); strncpy(bp_ω,loop_top,63);
        loop_push(ω, loop_top);
        emit_expr(body, bp_γ, bp_ω, ba,bb);
        loop_pop();
        Jmp(ba);

        Ldef(loop_top); Jmp(ca);
    } else {
        Jmp(ca);
    }

    Ldef(a); Jmp(ca);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * ICN_EVERY
 * ======================================================================= */
static void emit_every(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id=next_uid(); char a[64],b[64],gbfwd[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(gbfwd,sizeof gbfwd,"icon_%d_genb",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E("    ; EVERY  id=%d\n",id);

    EXPR_t *gen=n->children[0];
    EXPR_t *body=(n->nchildren>1)?n->children[1]:NULL;
    char ga[64],gb[64];

    if(body){
        char bstart[64]; snprintf(bstart,sizeof bstart,"icon_%d_body",id);
        char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ,gbfwd,63); strncpy(bp_ω,gbfwd,63);
        loop_push(ω, gbfwd);
        char ba[64],bb[64]; emit_expr(body, bp_γ, bp_ω, ba,bb);
        loop_pop();
        char gp_γ[64]; char gp_ω[64]; strncpy(gp_γ,bstart,63); strncpy(gp_ω,ω,63);
        emit_expr(gen, gp_γ, gp_ω, ga,gb);
        Ldef(bstart); Jmp(ba);
    } else {
        char gp_γ[64]; char gp_ω[64]; strncpy(gp_γ,gbfwd,63); strncpy(gp_ω,ω,63);
        emit_expr(gen, gp_γ, gp_ω, ga,gb);
    }
    Ldef(gbfwd); Jmp(gb);
    Ldef(a); Jmp(ga);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * ICN_AUGOP — E1 op:= E2   (augmented assignment)
 *
 * Semantics: E1 := E1 op E2  (E1 must be E_VAR)
 *
 * Strategy: build a synthetic binop/relop/concat node (two children: E1-copy
 * and E2), emit it, then store the result back into E1.
 *
 * The subtype is in n->ival (TK_AUGxxx enum value).  Map to the
 * corresponding ICN_xxx kind, emit as that node, then wrap in an assign store.
 * ======================================================================= */
#include "icon_lex.h"   /* TK_AUGPLUS etc. — needed for the subtype map */
static void emit_augop(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    if (n->nchildren < 2) { emit_fail_node(n, γ, ω, oa, ob); return; }

    EXPR_t *lhs = n->children[0];   /* must be E_VAR */
    EXPR_t *rhs = n->children[1];

    /* Map TK_AUGxxx → ICN_xxx */
    EKind op_kind;
    switch ((IcnTkKind)n->ival) {
        case TK_AUGPLUS:   op_kind = E_ADD;    break;
        case TK_AUGMINUS:  op_kind = E_SUB;    break;
        case TK_AUGSTAR:   op_kind = E_MUL;    break;
        case TK_AUGSLASH:  op_kind = E_DIV;    break;
        case TK_AUGMOD:    op_kind = E_MOD;    break;
        case TK_AUGCONCAT: op_kind = E_CAT; break;
        case TK_AUGEQ:     op_kind = E_EQ;     break;
        case TK_AUGSEQ:    op_kind = E_SSEQ;    break;
        case TK_AUGLT:     op_kind = E_LT;     break;
        case TK_AUGLE:     op_kind = E_LE;     break;
        case TK_AUGGT:     op_kind = E_GT;     break;
        case TK_AUGGE:     op_kind = E_GE;     break;
        case TK_AUGNE:     op_kind = E_NE;     break;
        default:
            /* Unhandled augop — fall back to fail */
            emit_fail_node(n, γ, ω, oa, ob); return;
    }

    int id = next_uid(); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    char store[64]; snprintf(store, sizeof store, "icon_%d_aug_store", id);

    /* Build a synthetic two-child node for the op on the stack.
     * We borrow lhs and rhs as children (no ownership transfer — they remain
     * owned by the parent ICN_AUGOP node). */
    EXPR_t syn;
    memset(&syn, 0, sizeof syn);
    syn.kind      = op_kind;
    syn.nchildren = 2;
    EXPR_t *ch[2] = { lhs, rhs };
    syn.children  = ch;

    /* Wire: op succeeds → store; op fails → ω */
    char op_γ[64]; char op_ω[64];
    strncpy(op_γ, store, 63);
    strncpy(op_ω, ω, 63);

    char opa[64], opb[64];
    emit_expr(&syn, op_γ, op_ω, opa, opb);

    /* α → op.α;  β → op.β */
    Ldef( a); Jmp( opa);
    Ldef( b); Jmp( opb);

    /* store: result is on hw stack (all binop/relop/concat push a value).
     * Pop it and write back into LHS variable. */
    Ldef( store);
    if (op_kind == E_CAT) {
        /* concat pushes a char* — pop into rax, store as pointer */
        E( "    pop     rax\n");
    } else {
        E( "    pop     rax\n");
    }
    if (lhs && lhs->kind == E_VAR) {
        int slot = locals_find(lhs->sval);
        if (slot >= 0) {
            E( "    mov     [rbp%+d], rax\n", slot_offset(slot));
        } else {
            char gv[80]; snprintf(gv, sizeof gv, "icn_gvar_%s", lhs->sval);
            bss_declare(gv);
            E( "    mov     [rel %s], rax\n", gv);
        }
    }
    Jmp( γ);
}

/* =========================================================================
 * Dispatch
 * ======================================================================= */
/* =========================================================================
 * G-9 gap-fill: Batch 1 — simple cases
 * ========================================================================= */

/* ICN_NONNULL — \E: succeed (keeping value) iff E succeeds (non-null check).
 * In our unboxed representation every value is "non-null"; just forward. */
static void emit_nonnull(EXPR_t *n, const char *γ, const char *ω,
                         char *oa, char *ob) {
    EXPR_t *child = n->nchildren > 0 ? n->children[0] : NULL;
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    char ca[64], cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ,γ,63); strncpy(cp_ω,ω,63);
    emit_expr(child, cp_γ, cp_ω, ca, cb);
    Ldef(a); Jmp(ca);
    Ldef(b); Jmp(cb);
}

/* ICN_REAL — floating-point literal: push truncated integer value.
 * For now: emit as integer (truncated). Full float support deferred. */
static void emit_real(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    long ival = (long)n->dval;
    Ldef(a);
    E("    push    %ld\n", ival);
    Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* ICN_SIZE — *E: size of string or list. Calls icn_strlen(ptr). */
static void emit_size(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], relay[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(relay,sizeof relay,"icon_%d_size_relay",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    EXPR_t *child = n->nchildren > 0 ? n->children[0] : NULL;
    char ca[64], cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ,relay,63); strncpy(cp_ω,ω,63);
    emit_expr(child, cp_γ, cp_ω, ca, cb);
    Ldef(a); Jmp(ca);
    Ldef(b); Jmp(cb);
    Ldef(relay);
    /* value (str ptr or int) on stack — treat as pointer, call icn_strlen */
    E("    pop     rdi\n");
    E("    call    icn_strlen\n");
    E("    push    rax\n");
    Jmp(γ);
}

/* ICN_POW — E1 ^ E2: integer exponentiation via icn_pow(base, exp). */
static void emit_pow(EXPR_t *n, const char *γ, const char *ω,
                     char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], got_r[64], got_l[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_r,sizeof got_r,"icon_%d_pow_gr",id);
    snprintf(got_l,sizeof got_l,"icon_%d_pow_gl",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int rc_slot = locals_alloc_tmp();
    char ra[64],rb[64],la[64],lb[64];
    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ,got_r,63); strncpy(rp_ω,ω,63);
    emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ,got_l,63); strncpy(lp_ω,ω,63);
    emit_expr(n->children[0], lp_γ, lp_ω, la, lb);
    Ldef(a); Jmp(la);
    Ldef(b); Jmp(rb);
    Ldef(got_r);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(rc_slot));
    Jmp(la);
    Ldef(got_l);
    E("    pop     rdi\n");                             /* base */
    E("    mov     rsi, [rbp%+d]\n", slot_offset(rc_slot)); /* exp */
    E("    call    icn_pow\n");
    E("    push    rax\n");
    Jmp(γ);
}

/* ICN_SEQ_EXPR — (E1; E2; ...; En): evaluate all, result is last value.
 * Each child's value is discarded except the last. */
static void emit_seq_expr(EXPR_t *n, const char *γ, const char *ω,
                          char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int nc = n->nchildren;
    if (nc == 0) { Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return; }
    /* Chain: child[i].γ → discard → child[i+1].α; child[i].ω → ω */
    char *next_α = NULL;
    char **alphas = malloc(nc * sizeof(char*));
    for (int i = 0; i < nc; i++) { alphas[i] = malloc(64); alphas[i][0] = '\0'; }
    char chain_lbl[64];
    strncpy(chain_lbl, γ, 63);  /* last child → γ */
    /* Emit children in reverse so we know next α */
    char prev_α[64]; strncpy(prev_α, γ, 63);
    for (int i = nc-1; i >= 0; i--) {
        char relay[64]; snprintf(relay,64,"icon_%d_seq_%d",id,i);
        char cp_γ[64]; char cp_ω[64];
        if (i == nc-1) { strncpy(cp_γ, γ, 63); }
        else           { strncpy(cp_γ, relay, 63); }
        strncpy(cp_ω, ω, 63);
        char ca[64], cb[64];
        emit_expr(n->children[i], cp_γ, cp_ω, ca, cb);
        strncpy(alphas[i], ca, 63);
        /* relay: discard value, jump to next child's α */
        if (i < nc-1) {
            Ldef(relay);
            E("    add     rsp, 8\n");  /* discard intermediate value */
            Jmp(alphas[i+1]);
        }
        (void)next_α; (void)prev_α;
    }
    Ldef(a); Jmp(alphas[0]);
    Ldef(b); Jmp(ω);
    for (int i = 0; i < nc; i++) free(alphas[i]);
    free(alphas);
    (void)chain_lbl;
}

/* ICN_IDENTICAL — E1 === E2: succeed iff same value (ptr equality for strings,
 * integer equality for ints). Uses icn_str_eq for strings. */
static void emit_identical(EXPR_t *n, const char *γ, const char *ω,
                           char *oa, char *ob) {
    if (n->nchildren < 2) {
        int id=next_uid(); char a[64],b[64];
        icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
        strncpy(oa,a,63); strncpy(ob,b,63);
        Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return;
    }
    int id = next_uid(); char a[64], b[64], got_r[64], got_l[64], chk[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_r,sizeof got_r,"icon_%d_id_gr",id);
    snprintf(got_l,sizeof got_l,"icon_%d_id_gl",id);
    snprintf(chk,  sizeof chk,  "icon_%d_id_chk",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int rc_slot = locals_alloc_tmp();
    char ra[64],rb[64],la[64],lb[64];
    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ,got_r,63); strncpy(rp_ω,ω,63);
    emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ,got_l,63); strncpy(lp_ω,ω,63);
    emit_expr(n->children[0], lp_γ, lp_ω, la, lb);
    Ldef(a); Jmp(la);
    Ldef(b); Jmp(rb);
    Ldef(got_r);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(rc_slot));
    Jmp(la);
    Ldef(got_l);
    /* Compare: both as integers (ptr or int — identical semantics) */
    E("    pop     rax\n");
    E("    cmp     rax, [rbp%+d]\n", slot_offset(rc_slot));
    E("    jne     %s\n", ω);
    E("    push    rax\n");
    Jmp(γ);
    (void)chk;
}

/* ICN_SWAP — a :=: b: swap two variables, result is new value of lhs. */
static void emit_swap(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    if (n->nchildren < 2 ||
        !n->children[0] || n->children[0]->kind != E_VAR ||
        !n->children[1] || n->children[1]->kind != E_VAR) {
        Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return;
    }
    EXPR_t *lv = n->children[0];
    EXPR_t *rv = n->children[1];
    int lslot = locals_find(lv->sval);
    int rslot = locals_find(rv->sval);
    Ldef(a);
    if (lslot >= 0 && rslot >= 0) {
        /* both locals — swap via tmp register */
        E("    mov     rax, [rbp%+d]\n", slot_offset(lslot));
        E("    mov     rcx, [rbp%+d]\n", slot_offset(rslot));
        E("    mov     [rbp%+d], rcx\n", slot_offset(lslot));
        E("    mov     [rbp%+d], rax\n", slot_offset(rslot));
        E("    push    rcx\n");  /* new value of lhs (old rhs) */
    } else {
        /* global(s) — use BSS var_<name> pattern */
        char lbss[80], rbss[80];
        snprintf(lbss,sizeof lbss,"icn_var_%s",lv->sval);
        snprintf(rbss,sizeof rbss,"icn_var_%s",rv->sval);
        bss_declare(lbss); bss_declare(rbss);
        E("    mov     rax, [rel %s]\n", lbss);
        E("    mov     rcx, [rel %s]\n", rbss);
        E("    mov     [rel %s], rcx\n", lbss);
        E("    mov     [rel %s], rax\n", rbss);
        E("    push    rcx\n");
    }
    Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* E_SGT/SGE/SLT/SLE/SNE — string relational operators.
 * Calls icn_str_cmp(a,b) then branches on result. */
static void emit_strrelop(EXPR_t *n, const char *γ, const char *ω,
                          char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], got_r[64], got_l[64], chk[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_r,sizeof got_r,"icon_%d_sr_gr",id);
    snprintf(got_l,sizeof got_l,"icon_%d_sr_gl",id);
    snprintf(chk,  sizeof chk,  "icon_%d_sr_chk",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int rc_slot = locals_alloc_tmp();
    char ra[64],rb[64],la[64],lb[64];
    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ,got_r,63); strncpy(rp_ω,ω,63);
    emit_expr(n->children[1], rp_γ, rp_ω, ra, rb);
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ,got_l,63); strncpy(lp_ω,ω,63);
    emit_expr(n->children[0], lp_γ, lp_ω, la, lb);
    Ldef(a); Jmp(la);
    Ldef(b); Jmp(rb);
    Ldef(got_r);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(rc_slot));
    Jmp(la);
    Ldef(got_l);
    E("    pop     rdi\n");                              /* left (a) */
    E("    mov     rsi, [rbp%+d]\n", slot_offset(rc_slot)); /* right (b) */
    E("    call    icn_str_cmp\n");
    /* rax = cmp result: <0, 0, >0 */
    E("    test    eax, eax\n");
    const char *jfail;
    switch(n->kind) {
        case E_SGT: jfail="jle"; break;
        case E_SGE: jfail="jl";  break;
        case E_SLT: jfail="jge"; break;
        case E_SLE: jfail="jg";  break;
        case E_SNE: jfail="je";  break;
        default:      jfail="jne"; break; /* E_SSEQ (==) */
    }
    E("    %s     %s\n", jfail, ω);
    /* push right value as result (string ptr still valid) */
    E("    push    qword [rbp%+d]\n", slot_offset(rc_slot));
    Jmp(γ);
    (void)chk;
}

/* ICN_REPEAT — repeat { body }: infinite loop, only exits via break.
 * Uses cur_break_label (set by the loop context push/pop below).
 * α → body.α; body_γ → loop_top; body_ω → loop_top (retry).
 * break inside body jumps to γ (or ω — we use γ). */

/* Loop control stack — parallel to loop nesting */
#define LOOP_MAX 32
static char icn_break_stack[LOOP_MAX][64];
static char icn_next_stack[LOOP_MAX][64];
static int  icn_loop_depth = 0;

static void loop_push(const char *brk, const char *nxt) {
    if (icn_loop_depth < LOOP_MAX) {
        strncpy(icn_break_stack[icn_loop_depth], brk, 63);
        strncpy(icn_next_stack[icn_loop_depth],  nxt, 63);
        icn_loop_depth++;
    }
}
static void loop_pop(void) { if (icn_loop_depth > 0) icn_loop_depth--; }
static const char *loop_break_target(void) {
    return icn_loop_depth > 0 ? icn_break_stack[icn_loop_depth-1] : "icn_loop_err";
}
static const char *loop_next_target(void) {
    return icn_loop_depth > 0 ? icn_next_stack[icn_loop_depth-1] : "icn_loop_err";
}

static void emit_repeat(EXPR_t *n, const char *γ, const char *ω,
                        char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], top[64], brk[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(top,sizeof top,"icon_%d_rep_top",id);
    snprintf(brk,sizeof brk,"icon_%d_rep_brk",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    EXPR_t *body = n->nchildren > 0 ? n->children[0] : NULL;
    loop_push(brk, top);
    char ba[64], bb[64];
    char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ,top,63); strncpy(bp_ω,top,63);
    if (body) emit_expr(body, bp_γ, bp_ω, ba, bb);
    loop_pop();
    Ldef(a);
    Ldef(top);
    if (body) Jmp(ba); else Jmp(top);
    Ldef(b); Jmp(ω);
    Ldef(brk); Jmp(γ);
}

/* ICN_BREAK — exit enclosing loop */
static void emit_break_node(EXPR_t *n, const char *γ, const char *ω,
                            char *oa, char *ob) {
    (void)n;    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *brk = loop_break_target();
    Ldef(a); Jmp(brk);
    Ldef(b); Jmp(brk);
}

/* ICN_NEXT — next iteration of enclosing loop */
static void emit_next_node(EXPR_t *n, const char *γ, const char *ω,
                           char *oa, char *ob) {
    (void)n;    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *nxt = loop_next_target();
    Ldef(a); Jmp(nxt);
    Ldef(b); Jmp(nxt);
}

/* ICN_INITIAL — initial { body }: runs body exactly once on first entry.
 * Uses a BSS flag per INITIAL node. */
static void emit_initial(EXPR_t *n, const char *γ, const char *ω,
                         char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], skip[64], flag[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(skip,sizeof skip,"icon_%d_init_skip",id);
    snprintf(flag,sizeof flag,"icn_init_flag_%d",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    bss_declare(flag);
    EXPR_t *body = n->nchildren > 0 ? n->children[0] : NULL;
    char ba[64], bb[64];
    char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ,skip,63); strncpy(bp_ω,skip,63);
    if (body) emit_expr(body, bp_γ, bp_ω, ba, bb);
    Ldef(a);
    E("    cmp     qword [rel %s], 0\n", flag);
    E("    jne     %s\n", skip);
    E("    mov     qword [rel %s], 1\n", flag);
    if (body) Jmp(ba); else Jmp(skip);
    Ldef(skip); Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * G-9 Batch 2 — moderate cases: LIMIT, SUBSCRIPT, SECTION, MAKELIST stub,
 *               FIELD stub, RECORD stub, CASE, BANG (generator iteration)
 * ========================================================================= */

/* ICN_LIMIT — E \ n: limit generator E to at most n results.
 * Uses a counter in a frame slot. */
static void emit_limit(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], got_lim[64], got_val[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_lim,sizeof got_lim,"icon_%d_lim_gl",id);
    snprintf(got_val,sizeof got_val,"icon_%d_lim_gv",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int lim_slot = locals_alloc_tmp();  /* stores limit count */
    int cnt_slot = locals_alloc_tmp();  /* stores remaining count */
    /* child[0]=generator, child[1]=limit */
    EXPR_t *gen = n->nchildren>0?n->children[0]:NULL;
    EXPR_t *lim = n->nchildren>1?n->children[1]:NULL;
    char ga[64],gb[64],la[64],lb[64];
    char gp_γ[64]; char gp_ω[64]; strncpy(gp_γ,got_val,63); strncpy(gp_ω,ω,63);
    emit_expr(gen, gp_γ, gp_ω, ga,gb);
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ,got_lim,63); strncpy(lp_ω,ω,63);
    emit_expr(lim, lp_γ, lp_ω, la,lb);
    /* α: eval limit once, store, set counter, start gen */
    Ldef(a);
    Jmp(la);
    Ldef(got_lim);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(lim_slot));
    E("    mov     [rbp%+d], rax\n", slot_offset(cnt_slot));
    Jmp(ga);
    /* got_val: gen produced a value; check counter */
    Ldef(got_val);
    E("    dec     qword [rbp%+d]\n", slot_offset(cnt_slot));
    E("    jl      %s\n", ω);  /* exhausted */
    Jmp(γ);
    /* β: resume gen if counter not exhausted */
    Ldef(b);
    E("    cmp     qword [rbp%+d], 0\n", slot_offset(cnt_slot));
    E("    jl      %s\n", ω);
    Jmp(gb);
}

/* ICN_SUBSCRIPT — lst[i] or str[i]: return element. Simple 1-based index. */
static void emit_subscript(EXPR_t *n, const char *γ, const char *ω,
                           char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], got_idx[64], got_obj[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_idx,sizeof got_idx,"icon_%d_sub_gi",id);
    snprintf(got_obj,sizeof got_obj,"icon_%d_sub_go",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int idx_slot = locals_alloc_tmp();
    EXPR_t *obj = n->nchildren>0?n->children[0]:NULL;
    EXPR_t *idx = n->nchildren>1?n->children[1]:NULL;
    char ia[64],ib[64],oa2[64],ob2[64];
    char ip_γ[64]; char ip_ω[64]; strncpy(ip_γ,got_idx,63); strncpy(ip_ω,ω,63);
    emit_expr(idx, ip_γ, ip_ω, ia,ib);
    char op2_γ[64]; char op2_ω[64]; strncpy(op2_γ,got_obj,63); strncpy(op2_ω,ω,63);
    emit_expr(obj, op2_γ, op2_ω, oa2,ob2);
    Ldef(a); Jmp(oa2);
    Ldef(b); Jmp(ib);
    Ldef(got_idx);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(idx_slot));
    Jmp(oa2);
    Ldef(got_obj);
    /* obj is a string ptr on stack; idx is 1-based integer */
    E("    pop     rdi\n");                                  /* string ptr */
    E("    mov     rsi, [rbp%+d]\n", slot_offset(idx_slot)); /* 1-based idx */
    E("    dec     rsi\n");                                   /* 0-based */
    E("    call    icn_str_subscript\n");
    E("    push    rax\n");
    Jmp(γ);
    (void)oa2; (void)ob2;
}

/* ICN_SECTION — s[i:j]: substring (1-based Icon convention). */
static void emit_section(EXPR_t *n, const char *γ, const char *ω,
                         char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    snprintf(a,sizeof a,"icn_%d_α",id); snprintf(b,sizeof b,"icn_%d_β",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    /* children: [obj, i, j] */
    EXPR_t *obj = n->nchildren>0?n->children[0]:NULL;
    EXPR_t *ifrom= n->nchildren>1?n->children[1]:NULL;
    EXPR_t *ito  = n->nchildren>2?n->children[2]:NULL;
    int i_slot = locals_alloc_tmp();
    int j_slot = locals_alloc_tmp();
    char oa2[64],ob2[64],ia[64],ib[64],ja[64],jb[64];
    char got_i[64],got_j[64],got_obj[64];
    snprintf(got_i,  sizeof got_i,   "icon_%d_sec_gi",id);
    snprintf(got_j,  sizeof got_j,   "icon_%d_sec_gj",id);
    snprintf(got_obj,sizeof got_obj, "icon_%d_sec_go",id);
    char ip_γ[64]; char ip_ω[64]; strncpy(ip_γ,got_i,63); strncpy(ip_ω,ω,63);
    emit_expr(ifrom, ip_γ, ip_ω, ia,ib);
    char jp_γ[64]; char jp_ω[64]; strncpy(jp_γ,got_j,63); strncpy(jp_ω,ω,63);
    emit_expr(ito, jp_γ, jp_ω, ja,jb);
    char op2_γ[64]; char op2_ω[64]; strncpy(op2_γ,got_obj,63); strncpy(op2_ω,ω,63);
    emit_expr(obj, op2_γ, op2_ω, oa2,ob2);
    Ldef(a); Jmp(oa2);
    Ldef(b); Jmp(ib);
    Ldef(got_i);
    E("    pop     rax\n"); E("    mov     [rbp%+d], rax\n", slot_offset(i_slot));
    Jmp(ja);
    Ldef(got_j);
    E("    pop     rax\n"); E("    mov     [rbp%+d], rax\n", slot_offset(j_slot));
    Jmp(oa2);
    Ldef(got_obj);
    /* call icn_str_section(ptr, i, j, kind) */
    E("    pop     rdi\n");  /* str ptr */
    E("    mov     rsi, [rbp%+d]\n", slot_offset(i_slot));
    E("    mov     rdx, [rbp%+d]\n", slot_offset(j_slot));
    long kind = (n->kind==E_SECTION_PLUS)?1:(n->kind==E_SECTION_MINUS)?2:0;
    E("    mov     rcx, %ld\n", kind);
    E("    call    icn_str_section\n");
    E("    push    rax\n");
    Jmp(γ);
    (void)oa2; (void)ob2;
}

/* E_MAKELIST — [e1,...,en]: stub — push 0 (list support deferred). */
static void emit_makelist(EXPR_t *n, const char *γ, const char *ω,
                          char *oa, char *ob) {
    (void)n;
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(a); E("    push    0\n"); Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* ICN_RECORD — record(f1,...) construction: stub — push 0. */
static void emit_record(EXPR_t *n, const char *γ, const char *ω,
                        char *oa, char *ob) {
    (void)n;
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(a); E("    push    0\n"); Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* ICN_FIELD — r.field: stub — push 0. */
static void emit_field(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    (void)n;
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(a); E("    push    0\n"); Jmp(γ);
    Ldef(b); Jmp(ω);
}

/* ICN_CASE — case E of { k1: b1 ... default: bd }
 * children[0]=selector, children[1..n-1]=arm pairs or default.
 * Parser encodes: odd children = key exprs, even = body exprs, last = default.
 * Simple approach: eval selector, eval each key, compare, branch. */
static void emit_case(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    if (n->nchildren < 1) { Ldef(a); Jmp(ω); Ldef(b); Jmp(ω); return; }
    int sel_slot = locals_alloc_tmp();
    char got_sel[64]; snprintf(got_sel,sizeof got_sel,"icon_%d_case_sel",id);
    EXPR_t *sel = n->children[0];
    char sa[64], sb[64];
    char sp_γ[64]; char sp_ω[64]; strncpy(sp_γ,got_sel,63); strncpy(sp_ω,ω,63);
    emit_expr(sel, sp_γ, sp_ω, sa,sb);
    Ldef(a); Jmp(sa);
    Ldef(b); Jmp(sb);
    Ldef(got_sel);
    E("    pop     rax\n");
    E("    mov     [rbp%+d], rax\n", slot_offset(sel_slot));
    /* Arms: children[1..] in pairs (key, body), optional trailing default */
    int narms = (n->nchildren - 1);
    int has_default = (narms % 2 == 1);
    int npairs = narms / 2;
    for (int i = 0; i < npairs; i++) {
        EXPR_t *key  = n->children[1 + i*2];
        EXPR_t *body = n->children[1 + i*2 + 1];
        char next_arm[64]; snprintf(next_arm,sizeof next_arm,"icon_%d_arm_%d",id,i);
        char got_key[64];  snprintf(got_key, sizeof got_key, "icon_%d_key_%d",id,i);
        char ka[64],kb[64];
        char kp_γ[64]; char kp_ω[64]; strncpy(kp_γ,got_key,63); strncpy(kp_ω,next_arm,63);
        emit_expr(key, kp_γ, kp_ω, ka,kb);
        Jmp(ka);
        Ldef(got_key);
        E("    pop     rax\n");
        E("    cmp     rax, [rbp%+d]\n", slot_offset(sel_slot));
        E("    jne     %s\n", next_arm);
        char ba2[64],bb2[64];
        char bp_γ[64]; char bp_ω[64]; strncpy(bp_γ,γ,63); strncpy(bp_ω,ω,63);
        emit_expr(body, bp_γ, bp_ω, ba2,bb2);
        Jmp(ba2);
        Ldef(next_arm);
    }
    if (has_default) {
        EXPR_t *def_body = n->children[n->nchildren-1];
        char da[64],db[64];
        char dp_γ[64]; char dp_ω[64]; strncpy(dp_γ,γ,63); strncpy(dp_ω,ω,63);
        emit_expr(def_body, dp_γ, dp_ω, da,db);
        Jmp(da);
    } else {
        Jmp(ω);
    }
}

/* =========================================================================
 * ICN_BANG — !E: string character generator
 * Implemented: 2026-03-29, G-9 s14 (was stub; BACKLOG-BANG-X64)
 *
 * α: eval E once → store char* in BSS str_slot, reset pos=0, fall to check
 * β: resume at check (string already in BSS slot)
 * check: pos >= icn_strlen(str) → ω
 *        else: call icn_bang_char_at(str, pos) → rax (char*)
 *              push rax; pos++; → γ
 *
 * BSS: per-node icn_N_bang_str (8 bytes, char*) + icn_N_bang_pos (8 bytes)
 * Lists (ICN_LIST children): stub as fail — list runtime not yet in x64.
 * ======================================================================= */
static void emit_bang(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    EXPR_t *child = (n->nchildren > 0) ? n->children[0] : NULL;

    /* List bang: not yet implemented in x64 runtime — stub */
    if (child && child->kind == E_MAKELIST) {
        Ldef(a); Jmp(ω);
        Ldef(b); Jmp(ω);
        return;
    }

    char str_bss[64], pos_bss[64], check[64], after_str[64];
    snprintf(str_bss,  sizeof str_bss,  "icon_%d_bang_str", id);
    snprintf(pos_bss,  sizeof pos_bss,  "icon_%d_bang_pos", id);
    snprintf(check,    sizeof check,    "icon_%d_bang_chk", id);
    snprintf(after_str,sizeof after_str,"icon_%d_bang_as",  id);
    bss_declare(str_bss);   /* char* — 8 bytes */
    bss_declare(pos_bss);   /* int64 — 8 bytes */

    /* Emit child expression; success → after_str (char* on stack) */
    char ca[64], cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ, after_str, 63); strncpy(cp_ω, ω, 63);
    if (child) emit_expr(child, cp_γ, cp_ω, ca, cb);
    else { snprintf(ca,64,"%s_noc",a); Ldef(ca); Jmp(ω); }

    /* after_str: child produced char* on hw stack → store, reset pos */
    Ldef( after_str);
    E("    pop     rax\n");
    E("    mov     [rel %s], rax\n", str_bss);
    E("    mov     qword [rel %s], 0\n", pos_bss);
    /* fall through to check */

    /* check: pos >= strlen(str) → ω; else char_at(str, pos) → push → γ */
    Ldef( check);
    E("    mov     rdi, [rel %s]\n", str_bss);
    E("    call    icn_strlen\n");          /* rax = length */
    E("    cmp     [rel %s], rax\n", pos_bss);
    E("    jge     %s\n", ω);
    E("    mov     rdi, [rel %s]\n", str_bss);
    E("    mov     rsi, [rel %s]\n", pos_bss);
    E("    call    icn_bang_char_at\n");    /* rax = char* */
    E("    push    rax\n");
    E("    inc     qword [rel %s]\n", pos_bss);
    Jmp( γ);

    /* α: start from child eval */
    Ldef(a); Jmp(ca);
    /* β: resume — string still in BSS, just re-check */
    Ldef(b); Jmp(check);
}

/* =========================================================================
 * ICN_MATCH — =E  (scan: succeed if subject starts with E, advance &pos)
 * Implemented: 2026-03-29, G-9 s14 (was stub; BACKLOG-BANG-X64)
 *
 * One-shot: α evals E → call icn_match_pat(pat) → rax (new pos or -1)
 * On success: push matched substring → γ. β → ω.
 *
 * icn_match_pat uses/updates global icn_subject / icn_pos (icon_runtime.c).
 * ======================================================================= */
static void emit_match(EXPR_t *n, const char *γ, const char *ω,
                       char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    char pat_bss[64], after_pat[64];
    snprintf(pat_bss,  sizeof pat_bss,  "icon_%d_match_pat", id);
    snprintf(after_pat,sizeof after_pat,"icon_%d_match_ap",  id);
    bss_declare(pat_bss);

    char ca[64], cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ, after_pat, 63); strncpy(cp_ω, ω, 63);
    if (n->nchildren > 0) emit_expr(n->children[0], cp_γ, cp_ω, ca, cb);
    else { snprintf(ca,64,"%s_noc",a); Ldef(ca); Jmp(ω); }

    Ldef( after_pat);
    E("    pop     rdi\n");                 /* pattern char* */
    E("    mov     [rel %s], rdi\n", pat_bss);
    E("    call    icn_match_pat\n");       /* rax = new pos or -1 */
    E("    cmp     rax, -1\n");
    E("    je      %s\n", ω);
    /* push matched portion (from old pos to new pos) — use icn_str_section */
    /* For now: push pat ptr as matched value (correct for literal patterns) */
    E("    mov     rax, [rel %s]\n", pat_bss);
    E("    push    rax\n");
    Jmp( γ);

    Ldef(a); Jmp(ca);
    Ldef(b); Jmp(ω);    /* one-shot */
}

/* ICN_BANG_BINARY — stub (binary list iteration, no x64 list runtime) */
static void emit_stub_fail(EXPR_t *n, const char *γ, const char *ω,
                           char *oa, char *ob) {
    (void)n;
    int id = next_uid(); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(a); Jmp(ω);
    Ldef(b); Jmp(ω);
}

/* =========================================================================
 * emit_cset_complement — ~E: cset complement via icn_cset_complement(ptr).
 * Child is a cset expr: may leave ptr in rdi (if E_CSET/E_QLIT literal)
 * or push a value.  Normalise via BSS slot then call runtime.
 * Result is a char* pushed onto stack (treated as cset/string value).
 * ======================================================================= */
static void emit_cset_complement(EXPR_t *n, const char *γ, const char *ω,
                                 char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], relay[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    snprintf(relay, sizeof relay, "icon_%d_csc_relay", id);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char cs_ptr[64]; snprintf(cs_ptr, sizeof cs_ptr, "icn_csc%d_ptr", id);
    bss_declare(cs_ptr);
    EXPR_t *child = n->nchildren > 0 ? n->children[0] : NULL;
    int child_is_cset = child && (child->kind == E_CSET || child->kind == E_QLIT);
    char ca[64], cb[64];
    char cp_γ[64]; char cp_ω[64]; strncpy(cp_γ, relay, 63); strncpy(cp_ω, ω, 63);
    emit_expr(child, cp_γ, cp_ω, ca, cb);
    Ldef( a); Jmp( ca);
    Ldef( b); Jmp( cb);
    Ldef( relay);
    if (child_is_cset) { E( "    mov     [rel %s], rdi\n", cs_ptr); }
    else               { E( "    pop     rax\n"); E( "    mov     [rel %s], rax\n", cs_ptr); }
    E( "    mov     rdi, [rel %s]\n", cs_ptr);
    E( "    call    icn_cset_complement\n");
    E( "    push    rax\n");
    Jmp( γ);
}

/* =========================================================================
 * emit_cset_binop — E1 ++ E2 / E1 -- E2 / E1 ** E2
 * Evaluate both children, normalise to BSS ptr slots, call runtime fn.
 * ======================================================================= */
static void emit_cset_binop(EXPR_t *n, const char *γ, const char *ω,
                            char *oa, char *ob) {
    int id = next_uid(); char a[64], b[64], lstore[64], compute[64], lbfwd[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    snprintf(lstore,  sizeof lstore,  "icon_%d_cbo_ls",  id);
    snprintf(compute, sizeof compute, "icon_%d_cbo_cmp", id);
    snprintf(lbfwd,   sizeof lbfwd,   "icon_%d_cbo_lb",  id);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char lc_ptr[64]; snprintf(lc_ptr, sizeof lc_ptr, "icn_cbo%d_lptr", id);
    char rc_ptr[64]; snprintf(rc_ptr, sizeof rc_ptr, "icn_cbo%d_rptr", id);
    bss_declare(lc_ptr); bss_declare(rc_ptr);
    EXPR_t *lch = n->nchildren > 0 ? n->children[0] : NULL;
    EXPR_t *rch = n->nchildren > 1 ? n->children[1] : NULL;
    int lcs = lch && (lch->kind == E_CSET || lch->kind == E_QLIT);
    int rcs = rch && (rch->kind == E_CSET || rch->kind == E_QLIT);
    char ra[64], rb[64];
    char rp_γ[64]; char rp_ω[64]; strncpy(rp_γ, compute, 63); strncpy(rp_ω, lbfwd, 63);
    emit_expr(rch, rp_γ, rp_ω, ra, rb);
    char la[64], lb[64];
    char lp_γ[64]; char lp_ω[64]; strncpy(lp_γ, lstore, 63); strncpy(lp_ω, ω, 63);
    emit_expr(lch, lp_γ, lp_ω, la, lb);
    Ldef( lbfwd); Jmp( lb);
    Ldef( a); Jmp( la);
    Ldef( b); Jmp( rb);
    Ldef( lstore);
    if (lcs) { E( "    mov     [rel %s], rdi\n", lc_ptr); }
    else      { E( "    pop     rax\n"); E( "    mov     [rel %s], rax\n", lc_ptr); }
    Jmp( ra);
    Ldef( compute);
    if (rcs) { E( "    mov     [rel %s], rdi\n", rc_ptr); }
    else      { E( "    pop     rax\n"); E( "    mov     [rel %s], rax\n", rc_ptr); }
    E( "    mov     rdi, [rel %s]\n", lc_ptr);
    E( "    mov     rsi, [rel %s]\n", rc_ptr);
    const char *fn = (n->kind == E_CSET_UNION) ? "icn_cset_union" :
                     (n->kind == E_CSET_DIFF)  ? "icn_cset_diff"  : "icn_cset_inter";
    E( "    call    %s\n", fn);
    E( "    push    rax\n");
    Jmp( γ);
}

static void emit_expr(EXPR_t *n, const char *γ, const char *ω,
                      char *oa, char *ob) {
    if(!n){ emit_fail_node(n, γ, ω, oa, ob); return; }
    switch(n->kind){
        case E_ILIT:    emit_int(n, γ, ω, oa, ob); break;
        case E_QLIT:    emit_str(n, γ, ω, oa, ob); break;
        case E_CSET:   emit_cset(n, γ, ω, oa, ob); break;
        case E_VAR:    emit_var(n, γ, ω, oa, ob); break;
        case E_ASSIGN: emit_assign(n, γ, ω, oa, ob); break;
        case E_RETURN: emit_return(n, γ, ω, oa, ob); break;
        case E_SUSPEND:emit_suspend(n, γ, ω, oa, ob); break;
        case E_FAIL:   emit_fail_node(n, γ, ω, oa, ob); break;
        case E_IF:     emit_if(n, γ, ω, oa, ob); break;
        case E_ALTERNATES:    emit_alt(n, γ, ω, oa, ob); break;
        case E_SCAN:   emit_scan(n, γ, ω, oa, ob); break;
        case E_MNS:    emit_neg(n, γ, ω, oa, ob); break;
        case E_NOT:    emit_not(n, γ, ω, oa, ob); break;
        case E_NULL:   emit_not(n, γ, ω, oa, ob); break;
        case E_SSEQ:    emit_seq(n, γ, ω, oa, ob); break;
        case E_CAT: case E_LCONCAT:
                         emit_concat(n, γ, ω, oa, ob); break;
        case E_ADD: case E_SUB: case E_MUL: case E_DIV: case E_MOD:
                         emit_binop(n, γ, ω, oa, ob); break;
        case E_LT: case E_LE: case E_GT: case E_GE: case E_EQ: case E_NE:
                         emit_relop(n, γ, ω, oa, ob); break;
        case E_TO:     emit_to(n, γ, ω, oa, ob); break;
        case E_TO_BY:  emit_to_by(n, γ, ω, oa, ob); break;
        case E_EVERY:  emit_every(n, γ, ω, oa, ob); break;
        case E_WHILE:  emit_while(n, γ, ω, oa, ob); break;
        case E_UNTIL:  emit_until(n, γ, ω, oa, ob); break;
        case E_FNC:   emit_call(n, γ, ω, oa, ob); break;
        case E_AUGOP:  emit_augop(n, γ, ω, oa, ob); break;
        /* G-9 gap-fill cases */
        case E_NONNULL:   emit_nonnull(n, γ, ω, oa, ob); break;
        case E_FLIT:      emit_real(n, γ, ω, oa, ob); break;
        case E_SIZE:      emit_size(n, γ, ω, oa, ob); break;
        case E_POW:       emit_pow(n, γ, ω, oa, ob); break;
        case E_SEQ_EXPR:  emit_seq_expr(n, γ, ω, oa, ob); break;
        case E_IDENTICAL: emit_identical(n, γ, ω, oa, ob); break;
        case E_SWAP:      emit_swap(n, γ, ω, oa, ob); break;
        case E_SGT: case E_SGE: case E_SLT:
        case E_SLE: case E_SNE:
                            emit_strrelop(n, γ, ω, oa, ob); break;
        case E_REPEAT:    emit_repeat(n, γ, ω, oa, ob); break;
        case E_LOOP_BREAK:     emit_break_node(n, γ, ω, oa, ob); break;
        case E_LOOP_NEXT:      emit_next_node(n, γ, ω, oa, ob); break;
        case E_INITIAL:   emit_initial(n, γ, ω, oa, ob); break;
        case E_LIMIT:     emit_limit(n, γ, ω, oa, ob); break;
        case E_IDX: emit_subscript(n, γ, ω, oa, ob); break;
        case E_SECTION: case E_SECTION_PLUS: case E_SECTION_MINUS:
                            emit_section(n, γ, ω, oa, ob); break;
        case E_MAKELIST:  emit_makelist(n, γ, ω, oa, ob); break;
        case E_RECORD:    emit_record(n, γ, ω, oa, ob); break;
        case E_FIELD:     emit_field(n, γ, ω, oa, ob); break;
        case E_CASE:      emit_case(n, γ, ω, oa, ob); break;
        case E_ITER:      emit_bang(n, γ, ω, oa, ob); break;
        case E_BANG_BINARY: emit_stub_fail(n, γ, ω, oa, ob); break;
        /* G1: ICN_POS — unary plus, identity: emit child unchanged */
        case E_PLS:    emit_expr(n->children[0], γ, ω, oa, ob); break;
        /* G2: ICN_RANDOM — ?E: random integer 1..E via icn_random() */
        case E_RANDOM: emit_random(n, γ, ω, oa, ob); break;
        /* G7: ICN_SCAN_AUGOP — E ?:= body: unimplemented, stub-fail to ω */
        case E_SCAN_AUGOP: {
            int id=next_uid(); char a2[64],b2[64];
            icn_label_α(id,a2,sizeof a2); icn_label_β(id,b2,sizeof b2);
            strncpy(oa,a2,63); strncpy(ob,b2,63);
            Ldef(a2); Jmp(ω);
            Ldef(b2); Jmp(ω);
            break;
        }
        /* G3–G6: cset operations */
        case E_CSET_COMPL:  emit_cset_complement(n, γ, ω, oa, ob); break;
        case E_CSET_UNION:  emit_cset_binop(n, γ, ω, oa, ob); break;
        case E_CSET_DIFF:   emit_cset_binop(n, γ, ω, oa, ob); break;
        case E_CSET_INTER:  emit_cset_binop(n, γ, ω, oa, ob); break;
        case E_PAT_SEQ: {
            /* n-ary conjunction: E1 & E2 & ... & En
             * irgen.icn ir_conjunction wiring:
             *   α → E1.α; E1_γ → E2.α; ...; En_γ → node_γ
             *   Ei_ω → E(i-1).β (backtrack left); E1_ω → node_ω
             *   β → En.β (resume rightmost)
             *
             * Fix: emit LEFT-TO-RIGHT so ccb[i-1] is already populated when
             * we wire Ei_ω.  Ei_γ needs E(i+1).α which isn't known yet, so
             * pre-generate a relay label for each child's γ; emit relay
             * trampolines (pop rax; jmp cca[i+1]) after all children. */
            int nc = n->nchildren;
            int cid = next_uid(); char ca2[64],cb2[64];
            icn_label_α(cid,ca2,sizeof ca2); icn_label_β(cid,cb2,sizeof cb2);
            strncpy(oa,ca2,63); strncpy(ob,cb2,63);

            char (*cca)[64]     = malloc(nc*64);
            char (*ccb)[64]     = malloc(nc*64);
            char (*relay_g)[64] = malloc(nc*64);
            for (int i = 0; i < nc; i++) {
                snprintf(relay_g[i], 64, "icon_%d_and_rg_%d", cid, i);
                cca[i][0] = '\0'; ccb[i][0] = '\0';
            }

            /* Emit children left-to-right */
            for (int i = 0; i < nc; i++) {
                char ep_γ[64]; char ep_ω[64];
                /* γ: last child → γ; otherwise relay_g[i] trampoline */
                strncpy(ep_γ, (i == nc-1) ? γ : relay_g[i], 63);
                /* ω: first child → ω; others ccb[i-1] (already filled) */
                strncpy(ep_ω, (i == 0) ? ω : ccb[i-1], 63);
                emit_expr(n->children[i], ep_γ, ep_ω, cca[i], ccb[i]);
            }

            /* Relay trampolines: discard Ei's value then jump to E(i+1).α */
            Jmp( ca2);
            for (int i = 0; i < nc-1; i++) {
                Ldef( relay_g[i]);
                E( "    add     rsp, 8\n");   /* discard Ei result */
                Jmp( cca[i+1]);
            }
            Ldef(ca2); Jmp(cca[0]);
            Ldef(cb2); Jmp(ccb[nc-1]);
            free(cca); free(ccb); free(relay_g);
            break;
        }
        default:{
            int id=next_uid(); char a2[64],b2[64];
            icn_label_α(id,a2,sizeof a2); icn_label_β(id,b2,sizeof b2);
            strncpy(oa,a2,63); strncpy(ob,b2,63);
            E("    ; UNIMPL %s id=%d\n",icn_kind_name(n->kind),id);
            Ldef(a2); Jmp(ω);
            Ldef(b2); Jmp(ω);
        }
    }
}

/* =========================================================================
 * icn_emit_file — full file emission
 * ======================================================================= */
void icn_emit_file(EXPR_t **nodes, int count, FILE *outf) {
    out = outf;
    bss_count=0; rodata_count=0; str_counter=0; user_proc_count=0;

    /* Pass 1: register all user procs, detect generators */
    for(int pi=0;pi<count;pi++){
        EXPR_t *proc=nodes[pi];
        if(!proc||proc->kind!=E_FNC||proc->nchildren<1) continue;
        const char *pname=proc->children[0]->sval;
        if(strcmp(pname,"main")==0) continue;
        int gen=0;
        int body_start_p=1+(int)proc->ival;
        for(int si=body_start_p;si<proc->nchildren;si++)
            if(has_suspend(proc->children[si])){ gen=1; break; }
        icn_register_proc(pname, (int)proc->ival, gen);
    }

    /* Emit to temp buffer */
    FILE *tmp=tmpfile(); FILE *real=out; out=tmp; uid=0;

    /* Declare globals needed by runtime */
    bss_declare("icn_retval");
    /* icn_failed is a byte — declare separately */

    for(int pi=0;pi<count;pi++){
        EXPR_t *proc=nodes[pi];
        if(!proc||proc->kind!=E_FNC||proc->nchildren<1) continue;
        const char *pname=proc->children[0]->sval;
        int is_main=strcmp(pname,"main")==0;

        E("\n; === procedure %s ===\n",pname);

        char proc_done[64]; snprintf(proc_done,sizeof proc_done, is_main?"icn_%s_done":"icn_u_%s_done",pname);
        char proc_ret[64];  snprintf(proc_ret, sizeof proc_ret,  is_main?"icn_%s_ret" :"icn_u_%s_ret", pname);
        char proc_sret[64]; snprintf(proc_sret,sizeof proc_sret, is_main?"icn_%s_sret":"icn_u_%s_sret",pname);

        int is_gen = !is_main && icn_is_gen_proc(pname);
        char caller_ret_bss[80];
        if(is_gen) snprintf(caller_ret_bss,sizeof caller_ret_bss,"icn_u_%s_caller_ret",pname);

        /* Setup local env */
        locals_reset();
        strncpy(cur_ret_label, is_main?"icn_main_done":proc_ret, 63);
        strncpy(cur_fail_label, proc_done, 63);
        strncpy(cur_suspend_ret_label, is_main?"icn_main_done":proc_sret, 63);

        /* Register params as local slots 0..np-1 */
        int np = (int)proc->ival;
        cur_nparams = np;
        for (int pi = 0; pi < np; pi++) {
            EXPR_t *pv = proc->children[1 + pi];
            if (pv && pv->kind == E_VAR)
                locals_add(pv->sval);
        }

        /* Scan for additional locals (E_GLOBAL nodes in body stmts) */
        int body_start = 1 + np;
        int nstmts = proc->nchildren - body_start;
        for (int si = 0; si < nstmts; si++) {
            EXPR_t *s = proc->children[body_start + si];
            if (s && s->kind == E_GLOBAL) {
                for (int ci = 0; ci < s->nchildren; ci++) {
                    EXPR_t *v = s->children[ci];
                    if (v && v->kind == E_VAR && locals_find(v->sval) < 0)
                        locals_add(v->sval);
                }
            }
        }
        /* Infer local var types from assignments (for write() dispatch) */
        infer_local_types(proc, body_start);

        /* Chain statements in reverse — skip E_GLOBAL (local decl) nodes */
        char **alphas=calloc(nstmts,sizeof(char*));
        for(int i=0;i<nstmts;i++) alphas[i]=malloc(64);
        char next_a[64]; strncpy(next_a,proc_done,63);

        for(int i=nstmts-1;i>=0;i--){
            EXPR_t *stmt=proc->children[body_start+i];
            if(!stmt||stmt->kind==E_GLOBAL){ strncpy(alphas[i],next_a,63); continue; }
            char sp_γ[64]; char sp_ω[64]; strncpy(sp_γ,next_a,63); strncpy(sp_ω,next_a,63);
            char sa[64],sb[64]; emit_expr(stmt, sp_γ, sp_ω, sa,sb);
            strncpy(alphas[i],sa,63); strncpy(next_a,sa,63);
        }

        /* Frame size computed AFTER emit so locals_alloc_tmp() slots are counted */
        int frame_size=(cur_nlocals>0)?8*(cur_nlocals+1):0;
        if(frame_size%16!=0) frame_size=(frame_size+15)&~15;

        /* Emit proc entry */
        E(is_main?"icn_%s:\n":"icn_u_%s:\n",pname);
        E("    push    rbp\n    mov     rbp, rsp\n");
        if(frame_size>0) E("    sub     rsp, %d\n",frame_size);
        /* Pop params from icn_stack into frame slots.
         * Caller pushes args left-to-right; pop in reverse → slot 0=first param. */
        if(!is_main && np>0){
            for(int pi=np-1;pi>=0;pi--){
                E("    call    icn_pop\n");
                E("    mov     [rbp%+d], rax\n", slot_offset(pi));
            }
        }
        if(nstmts>0) Jmp(alphas[0]);

        /* Return label (for non-main) */
        if(!is_main){
            if(is_gen){
                /* Generator: jmp-based — frame stays live between suspend/resume */
                Ldef(proc_ret);
                if(frame_size>0) E("    add     rsp, %d\n",frame_size);
                E("    pop     rbp\n");
                E("    jmp     [rel %s]\n", caller_ret_bss);
                /* Suspend-yield: frame stays live, just jump back to caller */
                Ldef(proc_sret);
                E("    jmp     [rel %s]\n", caller_ret_bss);
            } else {
                /* Normal proc: standard call/ret — safe for recursion */
                Ldef(proc_ret);
                if(frame_size>0) E("    add     rsp, %d\n",frame_size);
                E("    pop     rbp\n    ret\n");
            }
        }
        /* proc_done: procedure fell off end or explicit fail — signal failure to caller */
        Ldef(proc_done);
        E("    mov     byte [rel icn_failed], 1\n");
        if(is_gen){
            if(frame_size>0) E("    add     rsp, %d\n",frame_size);
            E("    pop     rbp\n");
            E("    jmp     [rel %s]\n", caller_ret_bss);
        } else if(!is_main){
            if(frame_size>0) E("    add     rsp, %d\n",frame_size);
            E("    pop     rbp\n    ret\n");
        } else {
            if(frame_size>0) E("    add     rsp, %d\n",frame_size);
            E("    pop     rbp\n    ret\n");
        }

        for(int i=0;i<nstmts;i++) free(alphas[i]);
        free(alphas);
    }

    /* Read temp body */
    long sz=ftell(tmp); rewind(tmp);
    char *body=malloc(sz+1); fread(body,1,sz,tmp); body[sz]='\0'; fclose(tmp);
    out=real;

    /* Emit file header */
    E("; Auto-generated by icon_emit.c — Tiny-ICON Byrd Box x64\n");
    E("bits 64\ndefault rel\n\n");

    if(rodata_count>0){
        E("section .rodata\n");
        for(int i=0;i<rodata_count;i++){
            E("%s:  db  ",rodata_entries[i].name);
            const char *s=rodata_entries[i].data;
            for(int j=0;s[j];j++){ if(j) E(", "); E("%d",(unsigned char)s[j]); }
            E(", 0\n"); free(rodata_entries[i].data);
        }
        E("\n");
    }

    E("section .bss\n");
    for(int i=0;i<bss_count;i++){
        /* skip symbols owned by C runtime */
        if(strcmp(bss_entries[i].name,"icn_suspended")==0) continue;
        if(strcmp(bss_entries[i].name,"icn_suspend_resume")==0) continue;
        if(strcmp(bss_entries[i].name,"icn_subject")==0) continue;
        if(strcmp(bss_entries[i].name,"icn_pos")==0) continue;
        E("    %s: resq 1\n",bss_entries[i].name);
    }
    /* Per-generator-proc caller_ret slots */
    for(int i=0;i<user_proc_count;i++){
        if(user_proc_is_gen[i])
            E("    icn_u_%s_caller_ret: resq 1\n", user_procs[i]);
    }
    E("    icn_failed: resb 1\n");
    E("    icn_suspended: resb 1\n");
    E("    icn_suspend_resume: resq 1\n");
    E("    icn_suspend_rbp: resq 1\n\n");

    E("section .text\n    global _start\n    extern icn_write_int\n    extern icn_write_str\n");
    E("    extern icn_push\n    extern icn_pop\n    extern icn_str_concat\n    extern icn_str_eq\n");
    E("    extern icn_any\n    extern icn_many\n    extern icn_upto\n");
    E("    extern icn_str_find\n    extern icn_match\n    extern icn_tab\n    extern icn_move\n");
    E("    extern icn_subject\n    extern icn_pos\n");
    E("    extern icn_str_cmp\n    extern icn_strlen\n    extern icn_pow\n");
    E("    extern icn_str_subscript\n    extern icn_str_section\n    extern icn_bang_char_at\n    extern icn_match_pat\n    extern icn_cset_complement\n    extern icn_cset_union\n    extern icn_cset_diff\n    extern icn_cset_inter\n\n");
    E("_start:\n    call    icn_main\n    mov     rax, 60\n    xor     rdi, rdi\n    syscall\n\n");

    fputs(body,real); free(body);
}

/* =========================================================================
 * Public API
 * ======================================================================= */
void icn_emit_expr(EXPR_t *n,
                      const char *γ, const char *ω, char *oa, char *ob) {
    emit_expr(n, γ, ω, oa,ob);
}
