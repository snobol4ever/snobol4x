/*
 * icon_emit_jvm.c — Tiny-ICON Byrd Box → JVM Jasmin emitter (Sprint IJ-1)
 *
 * Consumes the same IcnNode* AST as icon_emit.c.
 * Emits Jasmin assembler (.j) text assembled by jasmin.jar into .class files.
 *
 * Pipeline:
 *   icon_driver -jvm foo.icn -o foo.j
 *   java -jar jasmin.jar foo.j -d outdir/
 *   java -cp outdir/ FooClass
 *
 * Oracles:
 *   icon_emit.c        — Byrd-box wiring logic per ICN node (49KB, authoritative)
 *   emit_byrd_jvm.c    — Jasmin output format, helpers (187KB, copy verbatim where useful)
 *   jcon-master/tran/  — JCON gen_bc.icn / irgen.icn — blueprint
 *
 * Design:
 *   icon_emit_jvm.c = icon_emit.c logic + Jasmin output format
 *   Where icon_emit.c emits:  E(em, "    jmp  %s\n", lbl)
 *   We emit:                  J("    goto %s\n", lbl)
 *
 * Value representation:
 *   Icon integers → JVM long (lload/lstore, local slots N and N+1)
 *   Success/failure → static byte field icn_failed (0=ok, 1=failed)
 *   Suspend state  → static byte icn_suspended + int icn_suspend_id
 *   Return value   → static long icn_retval
 *
 * Label conventions (parallel to ASM):
 *   α icn_N_α   β icn_N_β   γ (inherited IjPorts.γ)   ω (inherited IjPorts.ω)
 *   Node γ:  (inherited, passed as string)
 *   Node ω:  (inherited, passed as string)
 *   Extra:   icn_N_code, icn_N_init, icn_N_lstore, etc.
 *
 * Local variable slots:
 *   Each local/param occupies 2 JVM slots (long is wide).
 *   slot_base(i) = 2*i
 *
 * Suspend/resume encoding:
 *   Each ICN_SUSPEND site gets a unique integer ID (1-based).
 *   icn_suspend_id static int field records which site suspended.
 *   Generator proc β entry does tableswitch on icn_suspend_id.
 *
 * Sprint IJ-1 scope: ICN_INT, ICN_VAR, ICN_ASSIGN, ICN_ADD/SUB/MUL/DIV/MOD,
 *   ICN_LT/LE/GT/GE/EQ/NE, ICN_TO, ICN_EVERY, ICN_CALL(write), ICN_IF,
 *   ICN_PROC, ICN_RETURN, ICN_FAIL, ICN_SUSPEND, ICN_ALT, ICN_WHILE
 */

#define _POSIX_C_SOURCE 200809L
#include "icon_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* =========================================================================
 * Output helpers
 * ======================================================================= */
static FILE *jout;

static void J(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(jout, fmt, ap); va_end(ap);
}
static void JI(const char *instr, const char *ops) {
    if (ops && ops[0]) J("    %s %s\n", instr, ops);
    else                J("    %s\n",   instr);
}
static void JL(const char *label) {
    J("%s:\n", label);
}
static void JGoto(const char *lbl) {
    J("    goto %s\n", lbl);
}
static void JC(const char *comment) {
    J(";  %s\n", comment);
}

/* =========================================================================
 * Class name
 * ======================================================================= */
static char ij_classname[256];

void ij_set_classname(const char *filename) {
    const char *base = strrchr(filename, '/');
    base = base ? base+1 : filename;
    int i = 0;
    for (const char *p = base; *p && *p != '.' && i < 254; p++, i++) {
        ij_classname[i] = isalnum((unsigned char)*p) ? *p : '_';
    }
    if (i == 0) { strcpy(ij_classname, "IconProg"); return; }
    /* Capitalize first letter for valid Java class name */
    ij_classname[0] = toupper((unsigned char)ij_classname[0]);
    ij_classname[i] = '\0';
}

/* =========================================================================
 * Node ID counter and label generation
 * ======================================================================= */
static int ij_node_id = 0;

static int ij_new_id(void) { return ij_node_id++; }

static void lbl_α  (int id, char *b, size_t s) { snprintf(b,s,"icn_%d_α",   id); }
static void lbl_β  (int id, char *b, size_t s) { snprintf(b,s,"icn_%d_β",   id); }
static void lbl_code(int id, char *b, size_t s) { snprintf(b,s,"icn_%d_code",id); }

/* =========================================================================
 * IcnPorts — inherited success/fail labels (same struct as icon_emit.h)
 * ======================================================================= */
typedef struct { char γ[64]; char ω[64]; } IjPorts;

/* =========================================================================
 * User procedure registry (mirrors icon_emit.c)
 * ======================================================================= */
#define MAX_USER_PROCS 64
static char ij_user_procs[MAX_USER_PROCS][64];
static int  ij_user_nparams[MAX_USER_PROCS];
static int  ij_user_is_gen[MAX_USER_PROCS];
static int  ij_user_count = 0;

static void ij_register_proc(const char *name, int nparams, int is_gen) {
    for (int i=0;i<ij_user_count;i++) if(!strcmp(ij_user_procs[i],name)) return;
    if (ij_user_count < MAX_USER_PROCS) {
        strncpy(ij_user_procs[ij_user_count], name, 63);
        ij_user_nparams[ij_user_count] = nparams;
        ij_user_is_gen[ij_user_count]  = is_gen;
        ij_user_count++;
    }
}
static int ij_is_user_proc(const char *name) {
    for (int i=0;i<ij_user_count;i++) if(!strcmp(ij_user_procs[i],name)) return 1;
    return 0;
}
static int ij_is_gen_proc(const char *name) {
    for (int i=0;i<ij_user_count;i++)
        if(!strcmp(ij_user_procs[i],name)) return ij_user_is_gen[i];
    return 0;
}
static int ij_nparams_for(const char *name) {
    for (int i=0;i<ij_user_count;i++)
        if(!strcmp(ij_user_procs[i],name)) return ij_user_nparams[i];
    return 0;
}
static int ij_has_suspend(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_SUSPEND) return 1;
    for (int i=0;i<n->nchildren;i++)
        if (ij_has_suspend(n->children[i])) return 1;
    return 0;
}

/* =========================================================================
 * Per-procedure local variable table
 * ======================================================================= */
#define MAX_LOCALS 32
typedef struct { char name[32]; int slot; } IjLocal;
static IjLocal ij_locals[MAX_LOCALS];
static int     ij_nlocals = 0;
static int     ij_nparams = 0;
static char    ij_ret_label[64]  = "";   /* label for return */
static char    ij_fail_label[64] = "";   /* label for proc fail */
static char    ij_sret_label[64] = "";   /* label for suspend-yield (frame kept) */
/* Suspend sites in current procedure */
static int     ij_suspend_ids[64];
static int     ij_suspend_count = 0;
static int     ij_next_suspend_id = 1;

static void ij_locals_reset(void) {
    ij_nlocals = 0; ij_nparams = 0;
    ij_suspend_count = 0;
}
static int ij_locals_find(const char *name) {
    for (int i=0;i<ij_nlocals;i++)
        if(!strcmp(ij_locals[i].name,name)) return ij_locals[i].slot;
    return -1;
}
static int ij_locals_add(const char *name) {
    int slot = ij_nlocals;
    if (ij_nlocals < MAX_LOCALS) {
        strncpy(ij_locals[ij_nlocals].name, name, 31);
        ij_locals[ij_nlocals++].slot = slot;
    }
    return slot;
}
static int ij_locals_alloc_tmp(void) {
    int slot = ij_nlocals;
    if (ij_nlocals < MAX_LOCALS) {
        ij_locals[ij_nlocals].name[0] = '\0';
        ij_locals[ij_nlocals++].slot = slot;
    }
    return slot;
}

/* Each named local uses 2 JVM slots (long).
 * JVM slot index = 2 * logical_slot */
static int slot_jvm(int logical) { return 2 * logical; }

/* Total JVM locals needed for .limit locals directive */
static int ij_jvm_locals_count(void) {
    return 2 * ij_nlocals + 2;   /* +2 for scratch */
}

/* Current procedure name — used to namespace per-proc static var fields */
static char ij_cur_proc[64] = "";

/* Static field name for a named local/param in current proc */
static void ij_var_field(const char *varname, char *out, size_t outsz) {
    snprintf(out, outsz, "icn_pv_%s_%s", ij_cur_proc, varname);
}

/* =========================================================================
 * Static fields emitted at class level
 * ======================================================================= */
#define MAX_STATICS 256
static char ij_statics[MAX_STATICS][64];
static char ij_static_types[MAX_STATICS];  /* 'J'=long, 'I'=int */
static int  ij_nstatics = 0;

static void ij_declare_static_typed(const char *name, char type) {
    for (int i=0;i<ij_nstatics;i++) if(!strcmp(ij_statics[i],name)) return;
    if (ij_nstatics < MAX_STATICS) {
        strncpy(ij_statics[ij_nstatics], name, 63);
        ij_static_types[ij_nstatics] = type;
        ij_nstatics++;
    }
}
static void ij_declare_static(const char *name) {
    ij_declare_static_typed(name, 'J');
}
static void ij_declare_static_int(const char *name) {
    ij_declare_static_typed(name, 'I');
}
static void ij_declare_static_str(const char *name) {
    ij_declare_static_typed(name, 'A');  /* 'A' = Ljava/lang/String; */
}

/* String pool for rodata */
#define MAX_STRINGS 256
static char ij_strings[MAX_STRINGS][512];
static int  ij_string_ids[MAX_STRINGS];
static int  ij_nstrings = 0;

static int ij_intern_string(const char *s) {
    for (int i=0;i<ij_nstrings;i++) if(!strcmp(ij_strings[i],s)) return ij_string_ids[i];
    int id = ij_nstrings;
    if (ij_nstrings < MAX_STRINGS) {
        strncpy(ij_strings[ij_nstrings], s, 511);
        ij_string_ids[ij_nstrings] = id;
        ij_nstrings++;
    }
    return id;
}

/* =========================================================================
 * Forward declaration
 * ======================================================================= */
static void ij_emit_expr(IcnNode *n, IjPorts ports,
                         char *out_α, char *out_β);

/* =========================================================================
 * Emit helpers for static field load/store
 * ======================================================================= */
static void ij_get_long(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s J", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_long(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s J", ij_classname, fname);
    JI("putstatic", buf);
}
static void ij_get_byte(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s B", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_byte(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s B", ij_classname, fname);
    JI("putstatic", buf);
}
static void ij_get_int_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s I", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_int_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s I", ij_classname, fname);
    JI("putstatic", buf);
}

/* String-typed static field helpers */
static void ij_get_str_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/lang/String;", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_str_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/lang/String;", ij_classname, fname);
    JI("putstatic", buf);
}

/* Forward declaration needed for ij_expr_is_string */
static int ij_expr_is_string(IcnNode *n);

/* Push 0 to icn_failed (success) */
static void ij_set_ok(void) {
    JI("iconst_0", ""); ij_put_byte("icn_failed");
}
/* Push 1 to icn_failed (failure) */
static void ij_set_fail(void) {
    JI("iconst_1", ""); ij_put_byte("icn_failed");
}
/* Test icn_failed — if nonzero jump to lbl */
static void ij_jmp_if_failed(const char *lbl) {
    ij_get_byte("icn_failed");
    J("    ifne %s\n", lbl);
}
/* Test icn_failed — if zero jump to lbl */
/* ij_jmp_if_ok removed — was unused */

/* =========================================================================
 * ICN_INT
 * α: push long literal, goto succeed
 * β: goto fail (one-shot)
 * ======================================================================= */
static void ij_emit_int(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JC("INT"); JL(a);
    J("    ldc2_w %ld\n", n->val.ival);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_STR
 * α: push String ref via ldc, goto succeed
 * β: goto fail
 * ======================================================================= */
static void ij_emit_str(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    (void)ij_intern_string(n->val.sval);
    JC("STR"); JL(a);
    /* Push string — ldc with quoted string */
    J("    ldc \"%s\"\n", n->val.sval);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_VAR
 * α: load from local slot (or static field for globals), goto succeed
 * β: goto fail (variables are one-shot)
 *
 * For long locals: lload slot_jvm(logical_slot)
 * For globals: getstatic classname/icn_gvar_NAME J
 * ======================================================================= */
static void ij_emit_var(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JC("VAR"); JL(a);

    /* &subject keyword — push current scan subject (String) */
    if (strcmp(n->val.sval, "&subject") == 0) {
        ij_declare_static_str("icn_subject");
        ij_get_str_field("icn_subject");
        JGoto(ports.γ);
        JL(b); JGoto(ports.ω);
        return;
    }

    int slot = ij_locals_find(n->val.sval);
    if (slot >= 0) {
        /* Named local/param: use per-proc static field (suspend-safe) */
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        /* Determine type: look up in statics table */
        int is_str = 0;
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'A') { is_str = 1; break; }
        if (is_str) {
            ij_declare_static_str(fld);
            ij_get_str_field(fld);
        } else {
            ij_declare_static(fld);
            ij_get_long(fld);
        }
    } else {
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        /* Check global type */
        int is_str = 0;
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'A') { is_str = 1; break; }
        if (is_str) { ij_declare_static_str(gname); ij_get_str_field(gname); }
        else        { ij_declare_static(gname);     ij_get_long(gname); }
    }
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_ASSIGN — E1 := E2
 * Evaluate RHS, store into LHS variable slot
 * ======================================================================= */
static void ij_emit_assign(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (n->nchildren < 2) {
        /* degenerate */
        int id = ij_new_id(); char a[64], b[64];
        lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
        strncpy(oα,a,63); strncpy(oβ,b,63);
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    char store[64]; snprintf(store, sizeof store, "icn_%d_store", id);

    IjPorts rp; strncpy(rp.γ, store, 63); strncpy(rp.ω, ports.ω, 63);
    char ra[64], rb[64];
    ij_emit_expr(n->children[1], rp, ra, rb);

    JL(a); JGoto(ra);
    JL(b); JGoto(rb);
    JL(store);

    IcnNode *lhs = n->children[0];
    IcnNode *rhs = n->children[1];
    int is_str = ij_expr_is_string(rhs);

    if (lhs && lhs->kind == ICN_VAR) {
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            if (is_str) {
                ij_declare_static_str(fld);
                ij_put_str_field(fld);
            } else {
                ij_declare_static(fld);
                ij_put_long(fld);
            }
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            if (is_str) { ij_declare_static_str(gname); ij_put_str_field(gname); }
            else        { ij_declare_static(gname);     ij_put_long(gname); }
        }
    } else {
        /* discard — pop appropriate type */
        if (is_str) JI("pop", "");
        else        JI("pop2", "");
    }
    /* Push back value for expression result (Icon := returns the value) */
    if (lhs && lhs->kind == ICN_VAR) {
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            if (is_str) ij_get_str_field(fld);
            else        ij_get_long(fld);
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            if (is_str) ij_get_str_field(gname);
            else        ij_get_long(gname);
        }
    } else {
        if (is_str) JI("aconst_null", "");
        else        JI("lconst_0", "");
    }
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_RETURN
 * Store value into icn_retval, set icn_failed=0, jump to ret label
 * ======================================================================= */
static void ij_emit_return(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    (void)ports;
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren > 0) {
        char after[64]; snprintf(after, sizeof after, "icn_%d_ret_store", id);
        char on_fail[64]; snprintf(on_fail, sizeof on_fail, "icn_%d_ret_fail", id);
        IjPorts vp; strncpy(vp.γ, after, 63); strncpy(vp.ω, on_fail, 63);
        char va[64], vb[64];
        ij_emit_expr(n->children[0], vp, va, vb);
        JL(a); JGoto(va);
        JL(b); JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
        /* on_fail: expr failed — no value on stack, jump to ret label */
        JL(on_fail);
        ij_set_fail();
        JGoto(ij_fail_label[0] ? ij_fail_label : "icn_dead");
        /* after: expr succeeded — long on stack */
        JL(after);
        ij_put_long("icn_retval");
        ij_set_ok();
        JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
    } else {
        JL(a);
        JI("lconst_0", ""); ij_put_long("icn_retval");
        ij_set_ok();
        JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
        JL(b);
        JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
    }
}

/* =========================================================================
 * ICN_FAIL
 * ======================================================================= */
static void ij_emit_fail_node(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    (void)n;
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JL(a);
    ij_set_fail();
    JGoto(ij_fail_label[0] ? ij_fail_label : ports.ω);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_SUSPEND — co-routine yield (user-defined generator)
 *
 * Encoding: each suspend site gets unique int ID.
 * α: evaluate E → store in icn_retval, set icn_suspended=1,
 *    set icn_suspend_id=N, set icn_failed=0, goto sret_label (yield)
 * β: indirect — the proc β entry does tableswitch → resume_here
 * resume_here: run optional body, then loop back to E's β
 * ======================================================================= */
static void ij_emit_suspend(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    (void)ports;
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Assign a unique suspend ID for tableswitch */
    int susp_id = ij_next_suspend_id++;
    if (ij_suspend_count < 64) ij_suspend_ids[ij_suspend_count++] = id;  /* Bug2 fix: store node id, not susp_id */

    char resume_here[64]; snprintf(resume_here, sizeof resume_here, "icn_%d_resume", id);
    char after_val[64];   snprintf(after_val,   sizeof after_val,   "icn_%d_yield",  id);

    IcnNode *val_node  = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *body_node = (n->nchildren > 1) ? n->children[1] : NULL;

    char va[64], vb[64];
    if (val_node) {
        IjPorts vp;
        strncpy(vp.γ, after_val, 63);
        strncpy(vp.ω, ij_fail_label[0] ? ij_fail_label : "icn_dead", 63);
        ij_emit_expr(val_node, vp, va, vb);
    } else {
        snprintf(va, 80, "icn_%d_noval", id);
        snprintf(vb, 80, "icn_%d_nvlb", id);
        JL(va); JI("lconst_0",""); JGoto(after_val);
        JL(vb); JGoto(ij_fail_label[0] ? ij_fail_label : "icn_dead");
    }

    /* α: enter value evaluation */
    JL(a); JGoto(va);
    /* β: resume via tableswitch in proc β — just a target label here */
    JL(b); JGoto(resume_here);   /* proc β will dispatch here via tableswitch */

    /* after_val: value (long) on JVM stack — yield to caller */
    JL(after_val);
    ij_put_long("icn_retval");
    ij_set_ok();
    /* Mark suspended with this site's ID */
    JI("iconst_1",""); ij_put_byte("icn_suspended");
    J("    sipush %d\n", susp_id); ij_put_int_field("icn_suspend_id");
    /* Yield — jump to sret (bare return from method, frame reclaimed) */
    JGoto(ij_sret_label[0] ? ij_sret_label : "icn_dead");

    /* resume_here: execution resumes here after proc β dispatches */
    if (body_node) {
        /* Fix IJ-6 Bug2: body assignment leaves a long result on the JVM operand
         * stack.  ports.γ is the while-loop top (icn_0_top), also reached from the
         * relop with an EMPTY stack.  Entering it with a stale long gives:
         *   "Inconsistent stack height 0 != 2"
         * Route body γ and ω through a drain label: pop2 then loop back. */
        char body_done[64]; snprintf(body_done, sizeof body_done, "icn_%d_bdone", id);
        char ba[64], bb[64];
        IjPorts bp;
        strncpy(bp.γ, body_done, 63);
        strncpy(bp.ω, ports.γ, 63);  /* body fail: empty stack → jump direct, no pop */
        ij_emit_expr(body_node, bp, ba, bb);
        JL(body_done); JI("pop2", ""); JGoto(ports.γ);
        JL(resume_here); JGoto(ba);
    } else {
        JL(resume_here); JGoto(ports.γ);
    }
}

/* =========================================================================
 * ICN_IF — if cond then E2 [else E3]
 * ======================================================================= */
static void ij_emit_if(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *cond  = n->children[0];
    IcnNode *thenb = (n->nchildren > 1) ? n->children[1] : NULL;
    IcnNode *elseb = (n->nchildren > 2) ? n->children[2] : NULL;

    char cond_then[64]; snprintf(cond_then, sizeof cond_then, "icn_%d_then", id);
    char cond_else[64]; snprintf(cond_else, sizeof cond_else, "icn_%d_else", id);

    char then_a[64]="", then_b[64]="", else_a[64]="";
    if (thenb) {
        IjPorts tp; strncpy(tp.γ,ports.γ,63); strncpy(tp.ω,ports.ω,63);
        ij_emit_expr(thenb, tp, then_a, then_b);
    } else { strncpy(then_a,ports.γ,63); }

    if (elseb) {
        IjPorts ep; strncpy(ep.γ,ports.γ,63); strncpy(ep.ω,ports.ω,63);
        char ea[64], eb[64];
        ij_emit_expr(elseb, ep, ea, eb);
        strncpy(else_a, ea, 63);
    } else { strncpy(else_a, ports.ω, 63); }

    IjPorts cp; strncpy(cp.γ,cond_then,63); strncpy(cp.ω,cond_else,63);
    char ca[64], cb[64];
    ij_emit_expr(cond, cp, ca, cb);

    /* cond_then: condition succeeded — value (long) on stack, discard */
    JL(cond_then);
    JI("pop2","");   /* discard condition result */
    JGoto(thenb ? then_a : ports.γ);

    JL(cond_else);
    JGoto(else_a);

    JL(a); JGoto(ca);
    JL(b); JGoto(cb);
}

/* =========================================================================
 * ICN_CALL — write() built-in or user procedure
 * ======================================================================= */
static void ij_emit_call(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (n->nchildren < 1) { ij_emit_fail_node(NULL,ports,oα,oβ); return; }
    IcnNode *fn = n->children[0];
    int nargs = n->nchildren - 1;
    const char *fname = (fn->kind == ICN_VAR) ? fn->val.sval : "unknown";
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* --- built-in write --- */
    if (strcmp(fname, "write") == 0) {
        if (nargs == 0) {
            JL(a);
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            JI("ldc", "\"\"");
            JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
            JGoto(ports.γ);
            JL(b); JGoto(ports.ω);
            return;
        }
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_call", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(arg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(arg_b);
        JL(after);
        /* Value on stack: String ref or long depending on arg type */
        if (ij_expr_is_string(arg)) {
            /* String ref on stack — store to scratch, push stream, load, println */
            int scratch = ij_locals_alloc_tmp();
            J("    astore %d\n", slot_jvm(scratch));
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            J("    aload %d\n", slot_jvm(scratch));
            JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
            /* write() returns its argument — reload String for caller */
            J("    aload %d\n", slot_jvm(scratch));
        } else {
            /* Long on stack — print as integer */
            int scratch = ij_locals_alloc_tmp();
            J("    lstore %d\n", slot_jvm(scratch));
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            J("    lload %d\n", slot_jvm(scratch));
            JI("invokevirtual", "java/io/PrintStream/println(J)V");
            /* Reload: write() returns its argument */
            J("    lload %d\n", slot_jvm(scratch));
        }
        JGoto(ports.γ);
        return;
    }

    /* --- user procedure call --- */
    if (ij_is_user_proc(fname)) {
        int is_gen = ij_is_gen_proc(fname);
        int np = ij_nparams_for(fname);
        char do_call[64]; snprintf(do_call, sizeof do_call, "icn_%d_docall", id);

        /* Evaluate args left-to-right, store into static arg slots */
        /* We use static fields icn_arg_0, icn_arg_1, ... for simplicity */
        char prev_succ[64]; strncpy(prev_succ, do_call, 63);
        char (*arg_alphas)[64] = nargs > 0 ? malloc(nargs * 64) : NULL;
        char (*arg_betas) [64] = nargs > 0 ? malloc(nargs * 64) : NULL;

        for (int i = nargs-1; i >= 0; i--) {
            char push_relay[64]; snprintf(push_relay, sizeof push_relay, "icn_%d_arg%d", id, i);
            IjPorts ap; strncpy(ap.γ, push_relay, 63); strncpy(ap.ω, ports.ω, 63);
            ij_emit_expr(n->children[i+1], ap, arg_alphas[i], arg_betas[i]);
            char argfield[64]; snprintf(argfield, sizeof argfield, "icn_arg_%d", i);
            ij_declare_static(argfield);
            JL(push_relay);
            ij_put_long(argfield);
            JGoto(prev_succ);
            strncpy(prev_succ, arg_alphas[i], 63);
        }

        JL(a);
        if (nargs > 0) JGoto(prev_succ);
        else           JGoto(do_call);

        /* β: for generators, resume if suspended; else fail */
        JL(b);
        if (is_gen) {
            char b_resume[64]; snprintf(b_resume, sizeof b_resume, "icn_%d_b_resume", id);
            char b_fail[64];   snprintf(b_fail,   sizeof b_fail,   "icn_%d_b_fail",   id);
            ij_get_byte("icn_suspended");
            J("    ifeq %s\n", b_fail);
            /* Resume: clear suspend_id AFTER we're done — entry dispatch uses icn_suspend_id!=0 */
            /* (icn_suspended stays set until proc itself clears on fresh call) */
            /* Just invoke — proc entry dispatches via icn_suspend_id */
            char sig[384]; snprintf(sig, sizeof sig, "%s/icn_%s()V", ij_classname, fname);
            JI("invokestatic", sig);
            /* Check result */
            char after_resume[64]; snprintf(after_resume, sizeof after_resume, "icn_%d_after_resume", id);
            ij_jmp_if_failed(after_resume);
            ij_get_long("icn_retval");
            JGoto(ports.γ);
            JL(after_resume); JGoto(ports.ω);
            JL(b_fail); JGoto(ports.ω);
        } else {
            JGoto(ports.ω);
        }

        /* do_call: push args from static fields, invoke proc */
        JL(do_call);
        /* Load args into static arg fields — already done above.
         * Proc pops from icn_arg_0..N-1. */
        {
            char sig[384]; snprintf(sig, sizeof sig, "%s/icn_%s()V", ij_classname, fname);
            JI("invokestatic", sig);
        }
        /* Check icn_failed */
        char after_call[64]; snprintf(after_call, sizeof after_call, "icn_%d_after_call", id);
        ij_jmp_if_failed(after_call);
        /* Check if suspended (generator yielded) or returned */
        ij_get_long("icn_retval");
        JGoto(ports.γ);
        JL(after_call); JGoto(ports.ω);

        if (arg_alphas) free(arg_alphas);
        if (arg_betas)  free(arg_betas);
        (void)np;
        return;
    }

    /* Unknown — fail */
    JL(a); JGoto(ports.ω);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_ALT — E1 | E2 | ... | En  value alternation (n-ary flat)
 * α → E1.α; E1.ω → E2.α; ... ; En.ω → node.ω
 * β → E1.β
 * ======================================================================= */
static void ij_emit_alt(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    int nc = n->nchildren;
    char (*ca)[64] = malloc(nc*64);
    char (*cb)[64] = malloc(nc*64);

    for (int i = nc-1; i >= 0; i--) {
        IjPorts ep;
        strncpy(ep.γ, ports.γ, 63);
        strncpy(ep.ω, (i == nc-1) ? ports.ω : ca[i+1], 63);
        ij_emit_expr(n->children[i], ep, ca[i], cb[i]);
    }

    JL(a); JGoto(ca[0]);
    JL(b); JGoto(cb[0]);
    free(ca); free(cb);
}

/* =========================================================================
 * Binary arithmetic — funcs-set wiring (Proebsting §4.3)
 * JVM discipline: operand stack EMPTY at every label boundary.
 * Values passed across labels go through static fields (lc, rc, bf).
 * ======================================================================= */
static void ij_emit_binop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char compute[64]; snprintf(compute, sizeof compute, "icn_%d_compute", id);
    char lbfwd[64];   snprintf(lbfwd,   sizeof lbfwd,   "icn_%d_lb",     id);
    char lstore[64];  snprintf(lstore,  sizeof lstore,  "icn_%d_lstore", id);
    /* Bug 1 fix: use local slots instead of static fields to support recursion.
     * Static fields are class-global and get clobbered by recursive calls. */
    int lc_slot = ij_locals_alloc_tmp();   /* long: 2 JVM slots */
    int rc_slot = ij_locals_alloc_tmp();   /* long: 2 JVM slots */
    int bf_slot = ij_locals_alloc_tmp();   /* used as int (istore/iload) */
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay, 63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    IcnNode *lchild = n->children[0];
    int left_is_value = (lchild->kind == ICN_VAR || lchild->kind == ICN_INT ||
                         lchild->kind == ICN_STR || lchild->kind == ICN_CALL);

    /* left_relay: left long on stack → drain to lc_slot → goto lstore */
    JL(left_relay); J("    lstore %d\n", slot_jvm(lc_slot)); JGoto(lstore);
    /* right_relay: right long on stack → drain to rc_slot → goto compute */
    JL(right_relay); J("    lstore %d\n", slot_jvm(rc_slot)); JGoto(compute);

    JL(lbfwd); JGoto(lb);
    JL(a); JI("iconst_0",""); J("    istore %d\n", slot_jvm(bf_slot)); JGoto(la);
    JL(b);
    if (left_is_value) { JI("iconst_1",""); J("    istore %d\n", slot_jvm(bf_slot)); JGoto(la); }
    else { JGoto(rb); }

    /* lstore: lc_slot has left, decide ra vs rb based on bf_slot */
    JL(lstore);
    J("    iload %d\n", slot_jvm(bf_slot));
    J("    ifeq %s\n", ra);
    JGoto(rb);

    /* compute: lc_slot=left, rc_slot=right — stack empty */
    JL(compute);
    J("    lload %d\n", slot_jvm(lc_slot));   /* push left  = value2 (below) */
    J("    lload %d\n", slot_jvm(rc_slot));   /* push right = value1 (top)   */
    /* JVM: lsub/ldiv/lrem = value2 op value1 = left op right ✓ */
    switch (n->kind) {
        case ICN_ADD: JI("ladd",""); break;
        case ICN_SUB: JI("lsub",""); break;
        case ICN_MUL: JI("lmul",""); break;
        case ICN_DIV: JI("ldiv",""); break;
        case ICN_MOD: JI("lrem",""); break;
        default: break;
    }
    JGoto(ports.γ);
}


/* =========================================================================
 * Relational operators — goal-directed retry (Proebsting §4.3)
 * JVM discipline: stack empty at all label boundaries.
 * lc_field = left cache, rc_field = right staging.
 * ======================================================================= */
static void ij_emit_relop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char chk[64];    snprintf(chk,    sizeof chk,    "icn_%d_check",  id);
    char lbfwd[64];  snprintf(lbfwd,  sizeof lbfwd,  "icn_%d_lb",     id);
    char lstore[64]; snprintf(lstore, sizeof lstore, "icn_%d_lstore", id);
    /* Bug 1 fix: use local slots instead of static fields to support recursion. */
    int lc_slot = ij_locals_alloc_tmp();   /* long: 2 JVM slots */
    int rc_slot = ij_locals_alloc_tmp();   /* long: 2 JVM slots */
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay,  63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    /* left_relay: left long on stack → drain to lc_slot → goto lstore (→ ra) */
    JL(left_relay); J("    lstore %d\n", slot_jvm(lc_slot)); JGoto(lstore);
    /* right_relay: right long on stack → drain to rc_slot → goto chk */
    JL(right_relay); J("    lstore %d\n", slot_jvm(rc_slot)); JGoto(chk);

    JL(lbfwd); JGoto(lb);
    JL(a); JGoto(la);
    JL(b); JGoto(rb);

    /* lstore: lc_slot has left → go to right.α */
    JL(lstore); JGoto(ra);

    /* chk: lc_slot=left, rc_slot=right — stack empty */
    JL(chk);
    J("    lload %d\n", slot_jvm(lc_slot));   /* push left  = value2 */
    J("    lload %d\n", slot_jvm(rc_slot));   /* push right = value1 */
    /* lcmp: value2 vs value1 = left vs right → negative if left<right */
    JI("lcmp","");
    const char *jfail;
    switch (n->kind) {
        case ICN_LT: jfail = "ifge"; break;
        case ICN_LE: jfail = "ifgt"; break;
        case ICN_GT: jfail = "ifle"; break;
        case ICN_GE: jfail = "iflt"; break;
        case ICN_EQ: jfail = "ifne"; break;
        case ICN_NE: jfail = "ifeq"; break;
        default:     jfail = "ifne"; break;
    }
    J("    %s %s\n", jfail, rb);
    /* success: reload right as result value */
    J("    lload %d\n", slot_jvm(rc_slot));
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_TO — range generator inline counter (§4.4)
 * ======================================================================= */
static void ij_emit_to(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64], code[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b); lbl_code(id,code,sizeof code);
    char init[64]; snprintf(init,sizeof init,"icn_%d_init",id);
    char e1bf[64]; snprintf(e1bf,sizeof e1bf,"icn_%d_e1b",id);
    char e2bf[64]; snprintf(e2bf,sizeof e2bf,"icn_%d_e2b",id);
    /* Static fields for all cross-label values */
    char I_field[64];     snprintf(I_field,     sizeof I_field,     "icn_%d_I",     id);
    char bound_field[64]; snprintf(bound_field, sizeof bound_field, "icn_%d_bound", id);
    char e1cur_field[64]; snprintf(e1cur_field, sizeof e1cur_field, "icn_%d_e1cur", id);
    char e2seen_field[64];snprintf(e2seen_field,sizeof e2seen_field,"icn_%d_e2seen",id);
    /* Staging field: child exprs store their value here before jumping to init/code */
    char e1val_field[64]; snprintf(e1val_field, sizeof e1val_field, "icn_%d_e1val", id);
    char e2val_field[64]; snprintf(e2val_field, sizeof e2val_field, "icn_%d_e2val", id);
    ij_declare_static(I_field);
    ij_declare_static(bound_field);
    ij_declare_static(e1cur_field);
    ij_declare_static_int(e2seen_field);
    ij_declare_static(e1val_field);
    ij_declare_static(e2val_field);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Relay labels: drain stack before crossing to init */
    char e1_relay[64]; snprintf(e1_relay, sizeof e1_relay, "icn_%d_e1relay", id);
    char e2_relay[64]; snprintf(e2_relay, sizeof e2_relay, "icn_%d_e2relay", id);

    IjPorts e2p; strncpy(e2p.γ, e2_relay, 63); strncpy(e2p.ω, e1bf, 63);
    char e2a[64],e2b[64]; ij_emit_expr(n->children[1], e2p, e2a, e2b);
    IjPorts e1p; strncpy(e1p.γ, e1_relay, 63); strncpy(e1p.ω, ports.ω, 63);
    char e1a[64],e1b[64]; ij_emit_expr(n->children[0], e1p, e1a, e1b);

    /* e1_relay: E1 long on stack → store to e1val, jump to e2a */
    JL(e1_relay);
    ij_put_long(e1val_field);
    JGoto(e2a);

    /* e2_relay: E2 long on stack → store to e2val, jump to init */
    JL(e2_relay);
    ij_put_long(e2val_field);
    JGoto(init);

    JL(e1bf);
    JI("iconst_0",""); ij_put_int_field(e2seen_field);
    JGoto(e1b);
    JL(e2bf); JGoto(e2b);

    JL(a);
    JI("iconst_0",""); ij_put_int_field(e2seen_field);
    JGoto(e1a);
    /* β: increment I, loop to code — stack empty here */
    JL(b);
    ij_get_long(I_field);
    JI("lconst_1","");
    JI("ladd","");
    ij_put_long(I_field);
    JGoto(code);

    /* init: E1 in e1val_field, E2 in e2val_field — stack empty */
    JL(init);
    ij_get_long(e2val_field);
    ij_put_long(bound_field);
    char init_e2adv[64]; snprintf(init_e2adv, sizeof init_e2adv, "icn_%d_init_e2adv", id);
    ij_get_int_field(e2seen_field);
    J("    ifne %s\n", init_e2adv);
    /* First time: read E1 value */
    ij_get_long(e1val_field);
    ij_put_long(e1cur_field);
    ij_get_long(e1cur_field);
    ij_put_long(I_field);
    JI("iconst_1",""); ij_put_int_field(e2seen_field);
    JGoto(code);
    JL(init_e2adv);
    ij_get_long(e1cur_field);
    ij_put_long(I_field);
    JGoto(code);

    /* code: I <= bound? — stack empty */
    JL(code);
    ij_get_long(I_field);
    ij_get_long(bound_field);
    JI("lcmp","");
    J("    ifgt %s\n", e2bf);
    /* Push I as the generated value — consumer must drain before next label */
    ij_get_long(I_field);
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_EVERY
 * ======================================================================= */
static void ij_emit_every(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char gbfwd[64]; snprintf(gbfwd, sizeof gbfwd, "icn_%d_genb", id);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *gen  = n->children[0];
    IcnNode *body = (n->nchildren > 1) ? n->children[1] : NULL;
    char ga[64], gb[64];

    if (body) {
        /* bstart: generator yielded a value — drain it, then run body */
        char bstart[64]; snprintf(bstart, sizeof bstart, "icn_%d_body", id);
        IjPorts bp; strncpy(bp.γ,gbfwd,63); strncpy(bp.ω,gbfwd,63);
        char ba[64], bb[64]; ij_emit_expr(body, bp, ba, bb);
        IjPorts gp; strncpy(gp.γ,bstart,63); strncpy(gp.ω,ports.ω,63);
        ij_emit_expr(gen, gp, ga, gb);
        JL(bstart); JI("pop2",""); JGoto(ba);
    } else {
        IjPorts gp; strncpy(gp.γ,gbfwd,63); strncpy(gp.ω,ports.ω,63);
        ij_emit_expr(gen, gp, ga, gb);
    }
    /* gbfwd: generator yielded — drain value, kick beta to get next */
    JL(gbfwd); JI("pop2",""); JGoto(gb);
    JL(a); JGoto(ga);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_WHILE
 * ======================================================================= */
static void ij_emit_while(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *cond = n->children[0];
    IcnNode *body = (n->nchildren > 1) ? n->children[1] : NULL;

    char cond_ok[64];  snprintf(cond_ok,  sizeof cond_ok,  "icn_%d_condok", id);
    char loop_top[64]; snprintf(loop_top, sizeof loop_top, "icn_%d_top",    id);

    IjPorts cp; strncpy(cp.γ,cond_ok,63); strncpy(cp.ω,ports.ω,63);
    char ca[64], cb[64]; ij_emit_expr(cond, cp, ca, cb);

    JL(cond_ok);
    JI("pop2","");   /* discard condition value */
    if (body) {
        char ba[64], bb[64];
        IjPorts bp; strncpy(bp.γ,loop_top,63); strncpy(bp.ω,loop_top,63);
        ij_emit_expr(body, bp, ba, bb);
        JGoto(ba);
        JL(loop_top); JGoto(ca);
    } else {
        JGoto(ca);
    }
    JL(a); JGoto(ca);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * String type predicate and concat emitter
 * ======================================================================= */
static int ij_expr_is_string(IcnNode *n) {
    if (!n) return 0;
    switch (n->kind) {
        case ICN_STR:    return 1;
        case ICN_CONCAT: return 1;
        case ICN_CALL: {
            /* write(str_arg) returns its argument — check arg type */
            if (n->nchildren >= 2) {
                IcnNode *fn = n->children[0];
                if (fn && fn->kind == ICN_VAR && strcmp(fn->val.sval, "write") == 0)
                    return ij_expr_is_string(n->children[1]);
            }
            return 0;
        }
        case ICN_ASSIGN: {
            /* Assignment returns the RHS value */
            if (n->nchildren >= 2) return ij_expr_is_string(n->children[1]);
            return 0;
        }
        case ICN_SCAN: {
            /* Scan result type = body result type */
            if (n->nchildren >= 2) return ij_expr_is_string(n->children[1]);
            return 0;
        }
        case ICN_VAR: {
            /* &subject keyword is always a String */
            if (strcmp(n->val.sval, "&subject") == 0) return 1;
            /* Check if var's static field is typed 'A' (String) */
            char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
            for (int i = 0; i < ij_nstatics; i++)
                if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'A') return 1;
            /* Also check global var */
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
            for (int i = 0; i < ij_nstatics; i++)
                if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'A') return 1;
            return 0;
        }
        default: return 0;
    }
}

/* ICN_CONCAT — E1 || E2  (string concatenation)
 * Byrd-box wiring: same funcs-set pattern as binop (Proebsting §4.3).
 * Left/right values are String refs stored in String-typed static fields.
 * Compute: invokevirtual String.concat → result String ref on stack → ports.γ
 *
 * Stack discipline: EMPTY at every label. String refs passed via static fields.
 */
static void ij_emit_concat(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char compute[64]; snprintf(compute, sizeof compute, "icn_%d_compute", id);
    char lbfwd[64];   snprintf(lbfwd,   sizeof lbfwd,   "icn_%d_lb",     id);
    char lstore[64];  snprintf(lstore,  sizeof lstore,  "icn_%d_lstore", id);
    char lc_fld[64];  snprintf(lc_fld,  sizeof lc_fld,  "icn_%d_lc",     id);
    char rc_fld[64];  snprintf(rc_fld,  sizeof rc_fld,  "icn_%d_rc",     id);
    /* bf: track whether we are on fresh (0) or resume (1) path */
    int bf_slot = ij_locals_alloc_tmp();
    ij_declare_static_str(lc_fld);
    ij_declare_static_str(rc_fld);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay,  63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    IcnNode *lchild = n->children[0];
    int left_is_value = ij_expr_is_string(lchild);  /* strings are one-shot */

    /* left_relay: String ref on stack → astore into lc_fld → lstore */
    JL(left_relay); ij_put_str_field(lc_fld); JGoto(lstore);
    /* right_relay: String ref on stack → astore into rc_fld → compute */
    JL(right_relay); ij_put_str_field(rc_fld); JGoto(compute);

    JL(lbfwd); JGoto(lb);
    JL(a); JI("iconst_0",""); J("    istore %d\n", slot_jvm(bf_slot)); JGoto(la);
    JL(b);
    if (left_is_value) { JI("iconst_1",""); J("    istore %d\n", slot_jvm(bf_slot)); JGoto(la); }
    else { JGoto(rb); }

    JL(lstore);
    J("    iload %d\n", slot_jvm(bf_slot));
    J("    ifeq %s\n", ra);
    JGoto(rb);

    /* compute: lc_fld = left String, rc_fld = right String */
    JL(compute);
    ij_get_str_field(lc_fld);
    ij_get_str_field(rc_fld);
    JI("invokevirtual", "java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;");
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_SCAN — E ? body
 *
 * Byrd-box wiring (four-port, per JCON ir_a_Scan / JCON-ANALYSIS §E ? body):
 *
 *   α  → expr.α
 *   expr.γ (new subject on stack as String):
 *          save old icn_subject → old_subject_N
 *          save old icn_pos    → old_pos_N
 *          putstatic icn_subject  (consumes new String from stack)
 *          iconst_0 → putstatic icn_pos
 *          → body.α
 *   expr.ω → ports.ω  (expr failed → whole scan fails)
 *   body.γ → restore old_subject / old_pos → ports.γ
 *   body.ω → restore old_subject / old_pos → expr.β
 *             (expr is one-shot string → expr.β → ports.ω)
 *   β      → restore old_subject / old_pos → body.β
 *
 * Save slots: per-scan static fields old_subject_N (String) and old_pos_N (I).
 * ======================================================================= */
static void ij_emit_scan(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Per-scan save-restore static fields */
    char old_subj[64], old_pos[64];
    snprintf(old_subj, sizeof old_subj, "icn_scan_oldsubj_%d", id);
    snprintf(old_pos,  sizeof old_pos,  "icn_scan_oldpos_%d",  id);
    ij_declare_static_str(old_subj);
    ij_declare_static_int(old_pos);

    /* Ensure global subject/pos fields are declared */
    ij_declare_static_str("icn_subject");
    ij_declare_static_int("icn_pos");

    /* Child nodes */
    IcnNode *expr_node = (n->nchildren >= 1) ? n->children[0] : NULL;
    IcnNode *body_node = (n->nchildren >= 2) ? n->children[1] : NULL;

    /* Label names for intermediate wiring */
    char setup[64], body_fail_restore[64], body_ok_restore[64], beta_restore[64];
    snprintf(setup,             sizeof setup,             "icn_%d_scan_setup",    id);
    snprintf(body_fail_restore, sizeof body_fail_restore, "icn_%d_scan_bfail",    id);
    snprintf(body_ok_restore,   sizeof body_ok_restore,   "icn_%d_scan_bok",      id);
    snprintf(beta_restore,      sizeof beta_restore,       "icn_%d_scan_beta",     id);

    /* Wire expr: success → setup, fail → ports.ω */
    IjPorts ep;
    strncpy(ep.γ, setup,    63);
    strncpy(ep.ω, ports.ω,  63);
    char ea[64], eb[64];
    ij_emit_expr(expr_node, ep, ea, eb);

    /* Wire body: success → body_ok_restore, fail → body_fail_restore */
    IjPorts bp;
    strncpy(bp.γ, body_ok_restore,   63);
    strncpy(bp.ω, body_fail_restore,  63);
    char ba[64], bb[64];
    ij_emit_expr(body_node, bp, ba, bb);

    /* α → expr.α */
    JC("SCAN"); JL(a); JGoto(ea);

    /* β → restore → body.β */
    JL(b); JGoto(beta_restore);

    /* setup: expr.γ — new subject String is on JVM stack
     *   save old subject/pos, install new subject, reset pos, → body.α */
    JL(setup);
    JC("SCAN save+install");
    /* Stack has new subject String — save it in a temp by storing first,
     * then saving old subject, restoring new.  Use dup to keep on stack. */
    /* Strategy: dup the new subject, put into icn_subject, then save old
     * subject was already overwritten — so: save old first, then install.
     * But old is in icn_subject already and new is on stack.  Use a temp
     * approach: swap via old_subj slot. */
    /* Correct sequence:
     *   [stack: new_str]
     *   getstatic icn_subject → save to old_subj  (pops nothing, pushes old)
     *   putstatic old_subj                         (consumes old)
     *   getstatic icn_pos → save to old_pos
     *   putstatic old_pos
     *   [stack: new_str still there from expr.γ]
     *   putstatic icn_subject                      (consumes new_str)
     *   iconst_0 → putstatic icn_pos
     * PROBLEM: getstatic doesn't consume the new_str — but new_str is
     * already on the stack when we arrive here.  We must save icn_subject
     * BEFORE consuming new_str.  Use dup: */
    /* Actually at expr.γ the String ref is on the JVM operand stack.
     * We do NOT want to consume it yet.  So: */
    /*   dup                        ; [new new]
     *   → we don't need dup; instead: save old, then install new.
     * The new_str is on stack.  getstatic pushes old on top:
     *   [new old_subj]
     * That's 2 items on stack.  We need to putstatic old_subj first,
     * but that pops old_subj leaving [new].  Perfect: */
    ij_get_str_field("icn_subject");         /* push old subject */
    ij_put_str_field(old_subj);              /* save old subject; stack: [new_str] */
    ij_get_int_field("icn_pos");             /* push old pos */
    ij_put_int_field(old_pos);               /* save old pos;    stack: [new_str] */
    ij_put_str_field("icn_subject");         /* install new subject; stack: [] */
    J("    iconst_0\n");
    ij_put_int_field("icn_pos");             /* reset pos to 0 */
    JGoto(ba);                               /* → body.α */

    /* body.γ — body succeeded: restore subject/pos, → ports.γ */
    JL(body_ok_restore);
    JC("SCAN restore (body ok)");
    ij_get_str_field(old_subj); ij_put_str_field("icn_subject");
    ij_get_int_field(old_pos);  ij_put_int_field("icn_pos");
    JGoto(ports.γ);

    /* body.ω — body failed: restore subject/pos, → expr.β (retry = fail) */
    JL(body_fail_restore);
    JC("SCAN restore (body fail)");
    ij_get_str_field(old_subj); ij_put_str_field("icn_subject");
    ij_get_int_field(old_pos);  ij_put_int_field("icn_pos");
    JGoto(eb);   /* expr.β — one-shot string → expr.ω → ports.ω */

    /* β: restore subject/pos, → body.β */
    JL(beta_restore);
    JC("SCAN restore (outer beta)");
    ij_get_str_field(old_subj); ij_put_str_field("icn_subject");
    ij_get_int_field(old_pos);  ij_put_int_field("icn_pos");
    JGoto(bb);   /* body.β */
}

/* =========================================================================
 * Dispatch
 * ======================================================================= */
static void ij_emit_expr(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (!n) { ij_emit_fail_node(NULL,ports,oα,oβ); return; }
    switch (n->kind) {
        case ICN_INT:     ij_emit_int      (n,ports,oα,oβ); break;
        case ICN_STR:     ij_emit_str      (n,ports,oα,oβ); break;
        case ICN_VAR:     ij_emit_var      (n,ports,oα,oβ); break;
        case ICN_ASSIGN:  ij_emit_assign   (n,ports,oα,oβ); break;
        case ICN_RETURN:  ij_emit_return   (n,ports,oα,oβ); break;
        case ICN_SUSPEND: ij_emit_suspend  (n,ports,oα,oβ); break;
        case ICN_FAIL:    ij_emit_fail_node(n,ports,oα,oβ); break;
        case ICN_IF:      ij_emit_if       (n,ports,oα,oβ); break;
        case ICN_ALT:     ij_emit_alt      (n,ports,oα,oβ); break;
        case ICN_AND: {
            /* n-ary conjunction: E1 & E2 & ... & En
             * irgen.icn ir_conjunction: Ei.γ → E(i+1).α; Ei.ω → E(i-1).β; β → En.β */
            int nc = n->nchildren;
            int cid = ij_new_id(); char ca2[64], cb2[64];
            lbl_α(cid,ca2,sizeof ca2); lbl_β(cid,cb2,sizeof cb2);
            strncpy(oα,ca2,63); strncpy(oβ,cb2,63);
            char (*cca)[64] = malloc(nc*64);
            char (*ccb)[64] = malloc(nc*64);
            for (int i = nc-1; i >= 0; i--) {
                IjPorts ep;
                strncpy(ep.γ, (i == nc-1) ? ports.γ : cca[i+1], 63);
                strncpy(ep.ω,    (i == 0)    ? ports.ω    : ccb[i-1], 63);
                ij_emit_expr(n->children[i], ep, cca[i], ccb[i]);
            }
            JL(ca2); JGoto(cca[0]);
            JL(cb2); JGoto(ccb[nc-1]);
            free(cca); free(ccb);
            break;
        }
        case ICN_ADD: case ICN_SUB: case ICN_MUL: case ICN_DIV: case ICN_MOD:
                          ij_emit_binop    (n,ports,oα,oβ); break;
        case ICN_CONCAT:  ij_emit_concat   (n,ports,oα,oβ); break;
        case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE: case ICN_EQ: case ICN_NE:
                          ij_emit_relop    (n,ports,oα,oβ); break;
        case ICN_TO:      ij_emit_to       (n,ports,oα,oβ); break;
        case ICN_EVERY:   ij_emit_every    (n,ports,oα,oβ); break;
        case ICN_WHILE:   ij_emit_while    (n,ports,oα,oβ); break;
        case ICN_CALL:    ij_emit_call     (n,ports,oα,oβ); break;
        case ICN_SCAN:    ij_emit_scan     (n,ports,oα,oβ); break;
        default: {
            int id = ij_new_id(); char a2[64], b2[64];
            lbl_α(id,a2,sizeof a2); lbl_β(id,b2,sizeof b2);
            strncpy(oα,a2,63); strncpy(oβ,b2,63);
            J(";  UNIMPL %d id=%d\n", n->kind, id);
            JL(a2); JGoto(ports.ω);
            JL(b2); JGoto(ports.ω);
        }
    }
}

/* =========================================================================
 * Emit one procedure as a static method
 * ======================================================================= */
static void ij_emit_proc(IcnNode *proc, FILE *out_target) {
    FILE *save = jout;
    /* Emit proc body to a temp buffer first (so we know locals count) */
    FILE *tmp = tmpfile();
    jout = tmp;

    const char *pname = proc->children[0]->val.sval;
    int is_main = (strcmp(pname, "main") == 0);
    int np = (int)proc->val.ival;
    int is_gen = !is_main && ij_is_gen_proc(pname);
    int body_start = 1 + np;
    int nstmts = proc->nchildren - body_start;

    /* Setup local environment */
    ij_locals_reset();
    strncpy(ij_cur_proc, pname, 63);
    char proc_done[64];
    if (is_main) snprintf(proc_done, sizeof proc_done, "icn_main_done");
    else         snprintf(proc_done, sizeof proc_done, "icn_%s_done", pname);
    char proc_ret[64];  snprintf(proc_ret,  sizeof proc_ret,  "icn_%s_ret",  pname);
    char proc_sret[64]; snprintf(proc_sret, sizeof proc_sret, "icn_%s_sret", pname);

    strncpy(ij_ret_label,  is_main ? "icn_main_done" : proc_ret,  63);
    strncpy(ij_fail_label, proc_done, 63);
    strncpy(ij_sret_label, is_main ? "icn_main_done" : proc_sret, 63);
    ij_next_suspend_id = 1;

    /* Register params as locals 0..np-1 */
    ij_nparams = np;
    for (int i = 0; i < np; i++) {
        IcnNode *pv = proc->children[1 + i];
        if (pv && pv->kind == ICN_VAR) ij_locals_add(pv->val.sval);
    }
    /* Scan for local declarations in body */
    for (int si = 0; si < nstmts; si++) {
        IcnNode *s = proc->children[body_start + si];
        if (s && s->kind == ICN_GLOBAL) {
            for (int ci = 0; ci < s->nchildren; ci++) {
                IcnNode *v = s->children[ci];
                if (v && v->kind == ICN_VAR && ij_locals_find(v->val.sval) < 0)
                    ij_locals_add(v->val.sval);
            }
        }
    }

    /* Pre-pass (forward): register string-typed variable fields so ij_expr_is_string
     * works correctly when the reverse emit loop checks statement result types.
     * Walk assignments left-to-right: if RHS is a string expression, declare the
     * LHS var's static field as type 'A' (String) before emit begins. */
    for (int si = 0; si < nstmts; si++) {
        IcnNode *stmt = proc->children[body_start + si];
        if (!stmt || stmt->kind != ICN_ASSIGN || stmt->nchildren < 2) continue;
        IcnNode *lhs = stmt->children[0];
        IcnNode *rhs = stmt->children[1];
        if (!lhs || lhs->kind != ICN_VAR) continue;
        if (!ij_expr_is_string(rhs)) continue;
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            ij_declare_static_str(fld);
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            ij_declare_static_str(gname);
        }
    }

    /* Chain statements.
     * Fix IJ-6 Bug3: every top-level statement (assignment etc.) leaves its result
     * value (a long) on the JVM operand stack when it jumps to its γ port.  The γ
     * port of statement[i] is the α port of statement[i+1], which is ALSO entered
     * from other paths with an empty stack → "Inconsistent stack height 0 != 2".
     * Solution: route each statement's γ through a private sdrain label that pops
     * the result before forwarding to the real next_a. */
    char **alphas = calloc(nstmts, sizeof(char*));
    for (int i = 0; i < nstmts; i++) alphas[i] = malloc(64);
    char next_a[64]; strncpy(next_a, proc_done, 63);

    for (int i = nstmts-1; i >= 0; i--) {
        IcnNode *stmt = proc->children[body_start + i];
        if (!stmt || stmt->kind == ICN_GLOBAL) { strncpy(alphas[i], next_a, 63); continue; }
        /* Determine if statement produces a String (1-slot) or long (2-slot) result */
        int stmt_is_str = ij_expr_is_string(stmt);
        /* Build a drain label for this statement's success port */
        char sdrain[64]; snprintf(sdrain, sizeof sdrain, "icn_s%d_sdrain", i);
        IjPorts sp; strncpy(sp.γ, sdrain, 63); strncpy(sp.ω, next_a, 63);
        char sa[64], sb[64]; ij_emit_expr(stmt, sp, sa, sb);
        /* Emit the drain: pop result (1-slot String or 2-slot long) then fall through */
        J("%s:\n", sdrain);
        JI(stmt_is_str ? "pop" : "pop2", "");
        JGoto(next_a);
        strncpy(alphas[i], sa, 63);
        strncpy(next_a, sa, 63);
    }

    /* Generator β tableswitch (if applicable) */
    int total_susp = ij_suspend_count;
    if (is_gen && total_susp > 0) {
        char beta_entry[64]; snprintf(beta_entry, sizeof beta_entry, "icn_%s_beta", pname);
        JL(beta_entry);
        ij_get_int_field("icn_suspend_id");
        J("    tableswitch 1 %d\n", total_susp);
        for (int k = 0; k < total_susp; k++) {
            /* Find the resume label for this suspend ID */
            J("        icn_%d_resume\n", ij_suspend_ids[k]); /* approximate: real IDs from emit */
        }
        J("        default: %s\n", proc_done);
    }

    /* proc_done: failure exit (for non-main); icn_main_done for main */
    JL(proc_done);
    if (!is_main) {
        /* Clear suspend state so subsequent calls to this proc start fresh */
        JI("iconst_0", ""); ij_put_byte("icn_suspended");
        JI("iconst_0", ""); ij_put_int_field("icn_suspend_id");
        ij_set_fail();
        JI("return","");
        /* proc_ret: success return */
        JL(proc_ret);
        ij_set_ok();
        JI("return","");
        /* proc_sret: suspend-yield return */
        JL(proc_sret);
        JI("return","");
    } else {
        JI("return","");
    }

    /* Read body buffer */
    long sz = ftell(tmp); rewind(tmp);
    char *body = malloc(sz + 1); fread(body, 1, sz, tmp); body[sz] = '\0';
    fclose(tmp);
    jout = out_target;

    /* Emit method header */
    int jvm_locals = ij_jvm_locals_count() + 10; /* generous padding */
    J(".method public static icn_%s()V\n", pname);
    J("    .limit stack 16\n");
    J("    .limit locals %d\n", jvm_locals);

    /* For non-main procs, load params from static arg fields */
    /* NOTE: for generators, param load happens at fresh_entry (after zero-init).
     * For non-generators, load here since there is no zero-init block. */
    if (!is_main && np > 0 && !(is_gen && total_susp > 0)) {
        for (int i = 0; i < np; i++) {
            IcnNode *pv = proc->children[1 + i];
            const char *pname2 = (pv && pv->kind == ICN_VAR) ? pv->val.sval : "";
            char argfield[64]; snprintf(argfield, sizeof argfield, "icn_arg_%d", i);
            char fld[128]; ij_var_field(pname2, fld, sizeof fld);
            ij_declare_static(fld);
            ij_get_long(argfield);
            ij_put_long(fld);
        }
    }

    /* Entry: generator dispatch — if icn_suspend_id != 0, we are resuming → beta entry */
    if (is_gen && total_susp > 0) {
        char beta_entry[64]; snprintf(beta_entry, sizeof beta_entry, "icn_%s_beta", pname);
        char fresh_entry[64]; snprintf(fresh_entry, sizeof fresh_entry, "icn_%s_fresh", pname);
        /* Zero-init all JVM local slots so verifier sees consistent types at all
         * control-flow join points (fresh path + all tableswitch resume targets). */
        for (int s = 0; s < ij_nlocals; s++) {
            JI("lconst_0", "");
            J("    lstore %d\n", slot_jvm(s));
        }
        J("    getstatic %s/icn_suspend_id I\n", ij_classname);
        J("    ifne %s\n", beta_entry);
        JL(fresh_entry);
        /* Fresh entry: load params from arg fields into per-proc static var fields.
         * This must happen AFTER zero-init (so zero-init doesn't clobber them)
         * and only on fresh calls (not resume — param values persist in static fields). */
        if (np > 0) {
            for (int i = 0; i < np; i++) {
                IcnNode *pv2 = proc->children[1 + i];
                const char *pvname = (pv2 && pv2->kind == ICN_VAR) ? pv2->val.sval : "";
                char argfield[64]; snprintf(argfield, sizeof argfield, "icn_arg_%d", i);
                char fld[128]; ij_var_field(pvname, fld, sizeof fld);
                ij_declare_static(fld);
                ij_get_long(argfield);
                ij_put_long(fld);
            }
        }
    }
    /* Entry: jump to first statement */
    if (nstmts > 0 && alphas[0][0]) JGoto(alphas[0]);
    else JGoto(proc_done);

    fputs(body, jout);
    free(body);

    J(".end method\n\n");

    for (int i = 0; i < nstmts; i++) free(alphas[i]);
    free(alphas);
    jout = save;
}

/* =========================================================================
 * ij_emit_file — entry point
 * ======================================================================= */
void ij_emit_file(IcnNode **nodes, int count, FILE *out, const char *filename) {
    ij_node_id = 0;
    ij_user_count = 0;
    ij_nstatics = 0;
    ij_nstrings = 0;

    ij_set_classname(filename ? filename : "IconProg");

    /* Pass 1: register all user procs */
    for (int pi = 0; pi < count; pi++) {
        IcnNode *proc = nodes[pi];
        if (!proc || proc->kind != ICN_PROC || proc->nchildren < 1) continue;
        const char *pname = proc->children[0]->val.sval;
        if (strcmp(pname, "main") == 0) continue;
        int np = (int)proc->val.ival;
        int gen = 0;
        int body_start = 1 + np;
        for (int si = body_start; si < proc->nchildren; si++)
            if (ij_has_suspend(proc->children[si])) { gen = 1; break; }
        ij_register_proc(pname, np, gen);
    }

    /* Pass 2: emit each proc to a buffer */
    FILE *body_buf = tmpfile();
    FILE *save = jout;
    for (int pi = 0; pi < count; pi++) {
        IcnNode *proc = nodes[pi];
        if (!proc || proc->kind != ICN_PROC || proc->nchildren < 1) continue;
        ij_emit_proc(proc, body_buf);
    }

    /* Read proc body */
    long bsz = ftell(body_buf); rewind(body_buf);
    char *procs_text = malloc(bsz + 1); fread(procs_text, 1, bsz, body_buf); procs_text[bsz] = '\0';
    fclose(body_buf);
    jout = out;

    /* Emit class header */
    J("; Auto-generated by icon_emit_jvm.c — Tiny-ICON Byrd Box JVM\n");
    J(".class public %s\n", ij_classname);
    J(".super java/lang/Object\n\n");

    /* Static fields: byte flags */
    J(".field public static icn_failed B\n");
    J(".field public static icn_suspended B\n");
    J(".field public static icn_suspend_id I\n");

    /* Long fields */
    J(".field public static icn_retval J\n");
    /* Per-user-proc arg static fields (already in statics array from emit_call) */
    for (int i = 0; i < ij_nstatics; i++) {
        char type = ij_static_types[i];
        if (type == 'A')
            J(".field public static %s Ljava/lang/String;\n", ij_statics[i]);
        else
            J(".field public static %s %c\n", ij_statics[i], type);
    }
    J("\n");

    /* Static initializer: set icn_subject = "" (not null), icn_pos = 0
     * Only emit if scan was used (icn_subject will be in statics). */
    int has_scan_fields = 0;
    for (int i = 0; i < ij_nstatics; i++)
        if (!strcmp(ij_statics[i], "icn_subject")) { has_scan_fields = 1; break; }
    if (has_scan_fields) {
        J(".method static <clinit>()V\n");
        J("    .limit stack 2\n    .limit locals 0\n");
        J("    ldc \"\"\n");
        J("    putstatic %s/icn_subject Ljava/lang/String;\n", ij_classname);
        J("    iconst_0\n");
        J("    putstatic %s/icn_pos I\n", ij_classname);
        J("    return\n");
        J(".end method\n\n");
    }

    /* main method: calls icn_main */
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 4\n    .limit locals 1\n");
    J("    invokestatic %s/icn_main()V\n", ij_classname);
    J("    return\n");
    J(".end method\n\n");

    /* Emit all procedure methods */
    fputs(procs_text, jout);
    free(procs_text);
    jout = save;
}
