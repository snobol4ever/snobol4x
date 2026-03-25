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

/* Build "ClassName/methodSig" into a static buffer for JI() calls */
static const char *ij_classname_buf(const char *sig) {
    static char buf[512];
    snprintf(buf, sizeof buf, "%s/%s", ij_classname, sig);
    return buf;
}

/* Build "ClassName/fieldName descriptor" for getstatic/putstatic */
static const char *ij_classname_buf_fld(const char *fld, const char *desc) {
    static char buf2[512];
    snprintf(buf2, sizeof buf2, "%s/%s %s", ij_classname, fld, desc);
    return buf2;
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

/* =========================================================================
 * Loop label stack — for break/next
 * Each loop emitter pushes before emitting body, pops after.
 * break  → ij_loop_exit[top]    (= ports.ω of enclosing loop)
 * next   → ij_loop_next[top]    (= loop restart: top-of-body / cond label)
 * ======================================================================= */
#define IJ_LOOP_STACK_MAX 32
static char ij_loop_exit[IJ_LOOP_STACK_MAX][64];  /* ports.ω for break */
static char ij_loop_next[IJ_LOOP_STACK_MAX][64];  /* restart label for next */
static int  ij_loop_depth = 0;

static void ij_loop_push(const char *exit_lbl, const char *next_lbl) {
    if (ij_loop_depth < IJ_LOOP_STACK_MAX) {
        strncpy(ij_loop_exit[ij_loop_depth], exit_lbl, 63);
        strncpy(ij_loop_next[ij_loop_depth], next_lbl, 63);
        ij_loop_depth++;
    }
}
static void ij_loop_pop(void) {
    if (ij_loop_depth > 0) ij_loop_depth--;
}
static const char *ij_loop_break_target(void) {
    if (ij_loop_depth > 0) return ij_loop_exit[ij_loop_depth-1];
    return "icn_main_done";  /* fallback: top-level fail */
}
static const char *ij_loop_next_target(void) {
    if (ij_loop_depth > 0) return ij_loop_next[ij_loop_depth-1];
    return "icn_main_done";
}

static void ij_locals_reset(void) {
    ij_nlocals = 0; ij_nparams = 0;
    ij_suspend_count = 0;
    ij_loop_depth = 0;
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

/* Double-typed (D) static field helpers — for ICN_POW, real to-by */
static void ij_declare_static_real(const char *name) {
    ij_declare_static_typed(name, 'D');
}
static void ij_get_real_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s D", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_real_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s D", ij_classname, fname);
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
 * ICN_REAL
 * α: push double via ldc2_w, goto succeed
 * β: goto fail (one-shot literal)
 * Doubles are stored in 'D' static fields; write() uses println(D).
 * ======================================================================= */
static void ij_declare_static_dbl(const char *name) {
    ij_declare_static_typed(name, 'D');
}
static void ij_get_dbl(const char *fld) {
    J("    getstatic %s/%s D\n", ij_classname, fld);
}
static void ij_put_dbl(const char *fld) {
    J("    putstatic %s/%s D\n", ij_classname, fld);
}

static void ij_emit_real(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JC("REAL"); JL(a);
    /* Emit as double constant. Jasmin requires 'd' suffix for ldc2_w double literals
     * (without it Jasmin treats the value as float and widens, losing precision). */
    /* Ensure a decimal point is present so Jasmin treats it as double, not int.
     * %g may produce "2" for 2.0; append ".0" if no decimal or exponent present. */
    {
        char dbuf[64];
        snprintf(dbuf, sizeof dbuf, "%g", n->val.fval);
        int has_dot = 0;
        for (int ci = 0; dbuf[ci]; ci++)
            if (dbuf[ci] == '.' || dbuf[ci] == 'e' || dbuf[ci] == 'E' || dbuf[ci] == 'n') { has_dot = 1; break; }
        if (!has_dot) strncat(dbuf, ".0", sizeof dbuf - strlen(dbuf) - 1);
        J("    ldc2_w %sd\n", dbuf);
    }
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* Returns 1 if this node produces a double (real) value */
static int ij_expr_is_real(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_REAL) return 1;
    /* ICN_NEG: real if child is real */
    if (n->kind == ICN_NEG && n->nchildren >= 1)
        return ij_expr_is_real(n->children[0]);
    /* ICN_POW always returns double (Math.pow returns D) */
    if (n->kind == ICN_POW) return 1;
    /* Binop is real if either operand is real */
    if (n->kind == ICN_ADD || n->kind == ICN_SUB || n->kind == ICN_MUL ||
        n->kind == ICN_DIV || n->kind == ICN_MOD) {
        if (n->nchildren >= 2)
            return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    }
    /* Relops return the right-hand value on success: real if either operand is real */
    if (n->kind == ICN_LT || n->kind == ICN_LE || n->kind == ICN_GT ||
        n->kind == ICN_GE || n->kind == ICN_EQ || n->kind == ICN_NE) {
        if (n->nchildren >= 2)
            return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    }
    /* ICN_ALT: real if first alternative is real (all alternatives must be same type) */
    if (n->kind == ICN_ALT && n->nchildren >= 1)
        return ij_expr_is_real(n->children[0]);
    /* real() builtin call always returns double */
    if (n->kind == ICN_CALL && n->nchildren >= 1) {
        IcnNode *fn = n->children[0];
        if (fn && fn->kind == ICN_VAR && strcmp(fn->val.sval, "real") == 0) return 1;
    }
    /* ICN_TO_BY: real if any of start/end/step is real */
    if (n->kind == ICN_TO_BY && n->nchildren >= 3)
        return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1])
               || ij_expr_is_real(n->children[2]);
    /* ICN_TO: real if either bound is real */
    if (n->kind == ICN_TO && n->nchildren >= 2)
        return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    if (n->kind == ICN_VAR) {
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'D') return 1;
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'D') return 1;
    }
    return 0;
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
        int is_str = 0, is_dbl = 0;
        for (int i = 0; i < ij_nstatics; i++) {
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'A') { is_str = 1; break; }
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'D') { is_dbl = 1; break; }
        }
        if (is_str) {
            ij_declare_static_str(fld);
            ij_get_str_field(fld);
        } else if (is_dbl) {
            ij_declare_static_dbl(fld);
            ij_get_dbl(fld);
        } else {
            ij_declare_static(fld);
            ij_get_long(fld);
        }
    } else {
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        /* Check global type */
        int is_str = 0, is_dbl = 0;
        for (int i = 0; i < ij_nstatics; i++) {
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'A') { is_str = 1; break; }
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'D') { is_dbl = 1; break; }
        }
        if (is_str)      { ij_declare_static_str(gname); ij_get_str_field(gname); }
        else if (is_dbl) { ij_declare_static_dbl(gname); ij_get_dbl(gname); }
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
    int is_dbl = !is_str && ij_expr_is_real(rhs);

    if (lhs && lhs->kind == ICN_VAR) {
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            if (is_str)      { ij_declare_static_str(fld); ij_put_str_field(fld); }
            else if (is_dbl) { ij_declare_static_dbl(fld); ij_put_dbl(fld); }
            else             { ij_declare_static(fld);     ij_put_long(fld); }
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            if (is_str)      { ij_declare_static_str(gname); ij_put_str_field(gname); }
            else if (is_dbl) { ij_declare_static_dbl(gname); ij_put_dbl(gname); }
            else             { ij_declare_static(gname);     ij_put_long(gname); }
        }
    } else {
        /* discard — pop appropriate type */
        if (is_str) JI("pop", "");
        else        JI("pop2", "");  /* double and long both take 2 JVM slots */
    }
    /* Push back value for expression result (Icon := returns the value) */
    if (lhs && lhs->kind == ICN_VAR) {
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            if (is_str)      ij_get_str_field(fld);
            else if (is_dbl) ij_get_dbl(fld);
            else             ij_get_long(fld);
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            if (is_str)      ij_get_str_field(gname);
            else if (is_dbl) ij_get_dbl(gname);
            else             ij_get_long(gname);
        }
    } else {
        if (is_str)      JI("aconst_null", "");
        else if (is_dbl) JI("dconst_0", "");
        else             JI("lconst_0", "");
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

    /* cond_then: condition succeeded — value on stack, discard it.
     * Use pop for String (1 slot), pop2 for long/double (2 slots). */
    JL(cond_then);
    if (ij_expr_is_string(cond)) JI("pop","");
    else                         JI("pop2","");
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
        } else if (ij_expr_is_real(arg)) {
            /* Double on stack — print as real */
            int scratch = ij_locals_alloc_tmp();
            J("    dstore %d\n", slot_jvm(scratch));
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            J("    dload %d\n", slot_jvm(scratch));
            JI("invokevirtual", "java/io/PrintStream/println(D)V");
            J("    dload %d\n", slot_jvm(scratch));
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

    /* --- built-in integer(x) --- convert real or string to long */
    if (strcmp(fname, "integer") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_int_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_real(arg))        JI("d2l", "");   /* double → long */
        else if (ij_expr_is_string(arg)) {
            /* String → Long.parseLong */
            int scratch = ij_locals_alloc_tmp();
            J("    astore %d\n", slot_jvm(scratch));
            J("    aload %d\n",  slot_jvm(scratch));
            JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        }
        /* else already long — no conversion needed */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in real(x) --- convert long or string to double */
    if (strcmp(fname, "real") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_real_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_string(arg)) {
            int scratch = ij_locals_alloc_tmp();
            J("    astore %d\n", slot_jvm(scratch));
            J("    aload %d\n",  slot_jvm(scratch));
            JI("invokestatic", "java/lang/Double/parseDouble(Ljava/lang/String;)D");
        } else if (!ij_expr_is_real(arg)) {
            JI("l2d", "");  /* long → double */
        }
        /* else already double — no conversion */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in string(x) --- convert numeric to String */
    if (strcmp(fname, "string") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_str_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_string(arg)) {
            /* already a String — no conversion */
        } else if (ij_expr_is_real(arg)) {
            JI("invokestatic", "java/lang/Double/toString(D)Ljava/lang/String;");
        } else {
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        }
        JGoto(ports.γ);
        return;
    }

    /* --- built-in any(cs) ---
     * Only fires if 'any' is NOT a user-defined procedure in this file. */
    if (strcmp(fname, "any") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *csarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_after", id);
        char chk[64];   snprintf(chk,   sizeof chk,   "icn_%d_chk",  id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(csarg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(arg_b);
        JL(after);
        JC("any: cs String on stack");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        /* stack: cs_String */
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        /* invoke helper: icn_builtin_any(cs, subj, pos) → long newpos or -1 */
        JI("invokestatic", ij_classname_buf("icn_builtin_any(Ljava/lang/String;Ljava/lang/String;I)J"));
        /* result: long on stack — lconst_m1 check */
        J("    dup2\n");                         /* [res res] */
        J("    lconst_1\n"); J("    lneg\n");    /* [res res -1L] */
        J("    lcmp\n");                         /* [res cmp] */
        J("    ifne %s\n", chk);                /* cmp!=0 → ok (not -1) */
        J("    pop2\n");                         /* discard -1L result */
        JGoto(ports.ω);
        JL(chk);
        JC("any ok: advance icn_pos, push newpos");
        /* newpos is on stack as long = (icn_pos+2); update icn_pos = newpos-1 (0-based) */
        J("    dup2\n");
        J("    lconst_1\n"); J("    lsub\n");    /* newpos-1 = new 0-based pos */
        J("    l2i\n");
        ij_put_int_field("icn_pos");             /* store updated pos */
        /* newpos long remains on stack → ports.γ */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in many(cs) --- only if not user-defined */
    if (strcmp(fname, "many") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *csarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_after", id);
        char chk[64];   snprintf(chk,   sizeof chk,   "icn_%d_chk",  id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(csarg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(arg_b);
        JL(after);
        JC("many: cs String on stack");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_many(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n");
        J("    lconst_1\n"); J("    lneg\n");
        J("    lcmp\n");
        J("    ifne %s\n", chk);
        J("    pop2\n");
        JGoto(ports.ω);
        JL(chk);
        JC("many ok: advance icn_pos");
        J("    dup2\n");
        J("    lconst_1\n"); J("    lsub\n");
        J("    l2i\n");
        ij_put_int_field("icn_pos");
        JGoto(ports.γ);
        return;
    }

    /* --- built-in upto(cs) --- only if not user-defined */
    if (strcmp(fname, "upto") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *csarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_after", id);
        char step[64];  snprintf(step,  sizeof step,  "icn_%d_step",  id);
        char chk[64];   snprintf(chk,   sizeof chk,   "icn_%d_chk",  id);
        /* Allocate a static String field to hold the cset across resume */
        char cs_fld[64]; snprintf(cs_fld, sizeof cs_fld, "icn_upto_cs_%d", id);
        ij_declare_static_str(cs_fld);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(csarg, ap, arg_a, arg_b);
        /* α: evaluate cs, store, then enter step */
        JL(a); JGoto(arg_a);
        JL(after);
        JC("upto α: save cs, enter step");
        ij_put_str_field(cs_fld);         /* consume cs String from stack */
        JGoto(step);
        /* β: cs already saved, re-enter step (pos already advanced) */
        JL(b); JGoto(step);
        /* step: scan forward until char in cs or end */
        JL(step);
        JC("upto step");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field(cs_fld);
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_upto_step(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n");
        J("    lconst_1\n"); J("    lneg\n");
        J("    lcmp\n");
        J("    ifne %s\n", chk);
        J("    pop2\n");
        JGoto(ports.ω);
        JL(chk);
        JC("upto: advance icn_pos past match, yield pos");
        /* result long = matched position (1-based); advance icn_pos to pos (0-based = result) */
        J("    dup2\n");
        J("    l2i\n");
        ij_put_int_field("icn_pos");      /* icn_pos = result (0-based next scan start) */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in find(s1, s2) --- generator: every occurrence of s1 in s2
     * α: eval s1, eval s2, store both, reset pos=0, goto check
     * β: advance pos (pos was 1-based result; next search starts at pos), goto check
     * check: icn_builtin_find(s1,s2,pos) → -1L → ω; else store pos=result, push result → γ */
    if (strcmp(fname, "find") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        IcnNode *s1arg = n->children[1];
        IcnNode *s2arg = n->children[2];
        char after1[64], after2[64], chk[64], ok[64];
        snprintf(after1, sizeof after1, "icn_%d_fa1",  id);
        snprintf(after2, sizeof after2, "icn_%d_fa2",  id);
        snprintf(chk,    sizeof chk,    "icn_%d_fchk", id);
        snprintf(ok,     sizeof ok,     "icn_%d_fok",  id);
        char s1f[64], s2f[64], pf[64];
        snprintf(s1f, sizeof s1f, "icn_find_s1_%d",  id);
        snprintf(s2f, sizeof s2f, "icn_find_s2_%d",  id);
        snprintf(pf,  sizeof pf,  "icn_find_pos_%d", id);
        ij_declare_static_str(s1f);
        ij_declare_static_str(s2f);
        ij_declare_static_int(pf);
        IjPorts ap1; strncpy(ap1.γ, after1, 63); strncpy(ap1.ω, ports.ω, 63);
        char a1[64], b1[64];
        ij_emit_expr(s1arg, ap1, a1, b1);
        IjPorts ap2; strncpy(ap2.γ, after2, 63); strncpy(ap2.ω, ports.ω, 63);
        char a2[64], b2[64];
        ij_emit_expr(s2arg, ap2, a2, b2);
        /* α */
        JL(a); JGoto(a1);
        JL(after1); ij_put_str_field(s1f); JGoto(a2);
        JL(after2); ij_put_str_field(s2f);
        J("    iconst_0\n"); ij_put_int_field(pf);
        JGoto(chk);
        /* β: last result was 1-based idx; next search from that same 0-based pos (idx itself) */
        JL(b);
        ij_get_int_field(pf);            /* pf holds 1-based last result */
        ij_put_int_field(pf);            /* no change — s2.indexOf scans from pf (0-based=pf-1+1=pf) */
        JGoto(chk);
        /* check */
        JL(chk);
        ij_get_str_field(s1f);
        ij_get_str_field(s2f);
        ij_get_int_field(pf);
        JI("invokestatic", ij_classname_buf("icn_builtin_find(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n    lconst_1\n    lneg\n    lcmp\n    ifne %s\n", ok);
        J("    pop2\n"); JGoto(ports.ω);
        JL(ok);
        JC("find ok: store result as new pos, push result → γ");
        J("    dup2\n    l2i\n"); ij_put_int_field(pf);
        JGoto(ports.γ);
        return;
    }

    /* --- built-in match(s) — one-shot: match s at current icn_pos
     * Calls icn_builtin_match(s, subject, pos) → new 1-based pos or -1L.
     * On success: advance icn_pos = result-1 (0-based), push result → γ. */
    if (strcmp(fname, "match") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        char after[64], ok[64];
        snprintf(after, sizeof after, "icn_%d_ma", id);
        snprintf(ok,    sizeof ok,    "icn_%d_mo", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(sarg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(ports.ω);
        JL(after);
        JC("match: s String on stack");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_match(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n    lconst_1\n    lneg\n    lcmp\n    ifne %s\n", ok);
        J("    pop2\n"); JGoto(ports.ω);
        JL(ok);
        JC("match ok: icn_pos = result-1 (0-based), push result → γ");
        J("    dup2\n    l2i\n    iconst_1\n    isub\n");
        ij_put_int_field("icn_pos");
        JGoto(ports.γ);
        return;
    }

    /* --- built-in tab(n) — one-shot: substring from icn_pos to n-1 (0-based), returns String
     * Calls icn_builtin_tab_str(n_int, subj, pos) → String or null on failure.
     * On success: icn_pos = n-1, push String → γ. */
    if (strcmp(fname, "tab") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *narg = n->children[1];
        char after[64], ok[64];
        snprintf(after, sizeof after, "icn_%d_ta", id);
        snprintf(ok,    sizeof ok,    "icn_%d_to", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(narg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(ports.ω);
        JL(after);
        JC("tab: n (long) on stack");
        J("    l2i\n");   /* convert long n to int */
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_tab_str(ILjava/lang/String;I)Ljava/lang/String;"));
        J("    dup\n    ifnonnull %s\n", ok);
        J("    pop\n"); JGoto(ports.ω);
        JL(ok);
        JC("tab ok: advance icn_pos = n-1, push String → γ");
        /* Helper already updated icn_pos — just push String */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in move(n) — one-shot: substring of length n from icn_pos, returns String
     * Calls icn_builtin_move_str(n_int, subj, pos) → String or null on failure.
     * On success: icn_pos = pos+n, push String → γ. */
    if (strcmp(fname, "move") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *narg = n->children[1];
        char after[64], ok[64];
        snprintf(after, sizeof after, "icn_%d_mva", id);
        snprintf(ok,    sizeof ok,    "icn_%d_mvo", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(narg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(ports.ω);
        JL(after);
        JC("move: n (long) on stack");
        J("    l2i\n");   /* convert long n to int */
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_move_str(ILjava/lang/String;I)Ljava/lang/String;"));
        J("    dup\n    ifnonnull %s\n", ok);
        J("    pop\n"); JGoto(ports.ω);
        JL(ok);
        JC("move ok: icn_pos advanced by helper, push String → γ");
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
 *
 * Naïve wiring (old, BUGGY for β-resume):
 *   α → E1.α; E1.ω → E2.α; ... ; En.ω → node.ω
 *   β → E1.β   ← always restarts from E1, loops infinitely in every
 *
 * Correct wiring — indirect-goto gate (JCON ir_a_Alt + JCON-ANALYSIS §'| value alternation'):
 *   Per-site static int field icn_N_alt_gate tracks which Ei last succeeded.
 *   α:    gate=0, goto E1.α
 *   Ei.γ: gate=i (1-based), goto ports.γ     (MoveLabel + succeed)
 *   Ei.ω: goto E(i+1).α  (En.ω → ports.ω)
 *   β:    iload gate → tableswitch → E1.β / E2.β / ... / En.β
 *
 * This matches JCON irgen.icn ir_MoveLabel + ir_IndirectGoto (§4.5).
 * ======================================================================= */
static void ij_emit_alt(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    int nc = n->nchildren;
    char (*ca)[64] = malloc(nc*64);
    char (*cb)[64] = malloc(nc*64);
    /* Per-alternative γ relay labels: we intercept Ei.γ to set the gate */
    char (*cg)[64] = malloc(nc*64);

    /* Gate field name — unique per alt site */
    char gate_fld[64]; snprintf(gate_fld, sizeof gate_fld, "icn_%d_alt_gate", id);
    ij_declare_static_int(gate_fld);

    /* Emit children right-to-left so each child's α/β/γ/ω are known
     * when we wire the chain. Each child's ports.γ → our relay cg[i],
     * ports.ω → next child's α (or ports.ω for last child). */
    for (int i = nc-1; i >= 0; i--) {
        /* relay label: child[i] success lands here → set gate, goto ports.γ */
        snprintf(cg[i], 64, "icn_%d_alt_g%d", id, i);
        IjPorts ep;
        strncpy(ep.γ, cg[i], 63);
        strncpy(ep.ω, (i == nc-1) ? ports.ω : ca[i+1], 63);
        ij_emit_expr(n->children[i], ep, ca[i], cb[i]);
    }

    /* Emit relay labels: cg[i]: gate = i+1 (1-based), goto ports.γ */
    for (int i = 0; i < nc; i++) {
        JL(cg[i]);
        J("    sipush %d\n", i+1);
        ij_put_int_field(gate_fld);
        JGoto(ports.γ);
    }

    /* α: gate=0 (mark "in α, no prior success"), goto E1.α */
    JL(a);
    JI("iconst_0",""); ij_put_int_field(gate_fld);
    JGoto(ca[0]);

    /* β: indirect-goto via gate → tableswitch → each Ei.β
     * gate values: 0 = never succeeded (shouldn't happen in bounded ctx, go to ω)
     *              1..nc = Ei last succeeded → resume Ei.β                         */
    JL(b);
    ij_get_int_field(gate_fld);
    J("    tableswitch 1 %d\n", nc);
    for (int i = 0; i < nc; i++) {
        J("        %s\n", cb[i]);
    }
    J("        default: %s\n", ports.ω);

    free(ca); free(cb); free(cg);
}

/* =========================================================================
 * ICN_POW — E1 ^ E2  (exponentiation, always returns double)
 * Wiring: standard funcs-set (§4.3): E1 then E2, compute, → γ.
 * Both operands promoted to double. Result = Math.pow(D,D) → double.
 * One-shot (β → ω) — pow is not a generator.
 * ======================================================================= */
static void ij_emit_pow(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char lrelay[64], rrelay[64], lstore[64], rstore[64];
    snprintf(lrelay, sizeof lrelay, "icn_%d_pow_lr", id);
    snprintf(rrelay, sizeof rrelay, "icn_%d_pow_rr", id);
    snprintf(lstore, sizeof lstore, "icn_%d_pow_lv", id);
    snprintf(rstore, sizeof rstore, "icn_%d_pow_rv", id);
    ij_declare_static_real(lstore);  /* D: left operand value */
    ij_declare_static_real(rstore);  /* D: right operand value */

    IcnNode *lchild = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *rchild = (n->nchildren > 1) ? n->children[1] : NULL;

    IjPorts rp; strncpy(rp.γ, rrelay, 63); strncpy(rp.ω, ports.ω, 63);
    IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);

    char ra[64], rb[64]; ij_emit_expr(rchild, rp, ra, rb);
    char la[64], lb[64]; ij_emit_expr(lchild, lp, la, lb);

    JC("POW -- E1 ^ E2 via Math.pow(D,D)");

    /* α: eval left */
    JL(a); JGoto(la);

    /* left γ: promote to double, store, eval right */
    JL(lrelay);
    if (!ij_expr_is_real(lchild)) { JI("l2d",""); }  /* long → double */
    ij_put_real_field(lstore);
    JGoto(ra);

    /* right γ: promote, store, then load left, load right, call Math.pow */
    JL(rrelay);
    if (!ij_expr_is_real(rchild)) { JI("l2d",""); }  /* long → double */
    ij_put_real_field(rstore);
    /* Now load in order: left (base), right (exponent) */
    ij_get_real_field(lstore);
    ij_get_real_field(rstore);
    JI("invokestatic","java/lang/Math/pow(DD)D");
    /* Result is D on stack → γ */
    JGoto(ports.γ);

    /* β: one-shot → ω */
    JL(b); JGoto(ports.ω);
}

 /* ==========================================================================
 * Binary arithmetic — funcs-set wiring (Proebsting §4.3)
 * JVM discipline: operand stack EMPTY at every label boundary.
 * Values passed across labels go through static fields (lc, rc, bf).
 * ========================================================================= */
static void ij_emit_binop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char compute[64]; snprintf(compute, sizeof compute, "icn_%d_compute", id);
    char lbfwd[64];   snprintf(lbfwd,   sizeof lbfwd,   "icn_%d_lb",     id);
    char lstore[64];  snprintf(lstore,  sizeof lstore,  "icn_%d_lstore", id);
    /* Determine if this is a double (real) operation */
    int is_dbl = ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    /* Bug 1 fix: use local slots instead of static fields to support recursion. */
    int lc_slot = ij_locals_alloc_tmp();   /* long or double: 2 JVM slots */
    int rc_slot = ij_locals_alloc_tmp();   /* long or double: 2 JVM slots */
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
                         lchild->kind == ICN_REAL ||
                         lchild->kind == ICN_STR || lchild->kind == ICN_CALL);

    const char *st_op = is_dbl ? "dstore" : "lstore";
    const char *ld_op = is_dbl ? "dload"  : "lload";

    /* left_relay: value on stack → promote if needed → drain to lc_slot */
    JL(left_relay);
    if (is_dbl && !ij_expr_is_real(n->children[0])) JI("l2d", "");
    J("    %s %d\n", st_op, slot_jvm(lc_slot)); JGoto(lstore);
    /* right_relay: value on stack → promote if needed → drain to rc_slot */
    JL(right_relay);
    if (is_dbl && !ij_expr_is_real(n->children[1])) JI("l2d", "");
    J("    %s %d\n", st_op, slot_jvm(rc_slot)); JGoto(compute);

    JL(lbfwd); JGoto(lb);
    JL(a); JI("iconst_0",""); J("    istore %d\n", slot_jvm(bf_slot)); JGoto(la);
    JL(b);
    if (left_is_value) { JI("iconst_1",""); J("    istore %d\n", slot_jvm(bf_slot)); JGoto(la); }
    else { JGoto(rb); }

    JL(lstore);
    J("    iload %d\n", slot_jvm(bf_slot));
    J("    ifeq %s\n", ra);
    JGoto(rb);

    /* compute: lc_slot=left, rc_slot=right — stack empty */
    JL(compute);
    J("    %s %d\n", ld_op, slot_jvm(lc_slot));
    J("    %s %d\n", ld_op, slot_jvm(rc_slot));
    if (is_dbl) {
        switch (n->kind) {
            case ICN_ADD: JI("dadd",""); break;
            case ICN_SUB: JI("dsub",""); break;
            case ICN_MUL: JI("dmul",""); break;
            case ICN_DIV: JI("ddiv",""); break;
            case ICN_MOD: JI("drem",""); break;
            default: break;
        }
    } else {
        switch (n->kind) {
            case ICN_ADD: JI("ladd",""); break;
            case ICN_SUB: JI("lsub",""); break;
            case ICN_MUL: JI("lmul",""); break;
            case ICN_DIV: JI("ldiv",""); break;
            case ICN_MOD: JI("lrem",""); break;
            default: break;
        }
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

    /* Determine if either operand is real — doubles need dstore/dload/dcmpl */
    int is_dbl = ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    const char *st_op = is_dbl ? "dstore" : "lstore";
    const char *ld_op = is_dbl ? "dload"  : "lload";

    int lc_slot = ij_locals_alloc_tmp();   /* long or double: 2 JVM slots */
    int rc_slot = ij_locals_alloc_tmp();   /* long or double: 2 JVM slots */
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay,  63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    /* left_relay: value on stack → promote to double if needed → drain to lc_slot */
    JL(left_relay);
    if (is_dbl && !ij_expr_is_real(n->children[0])) JI("l2d", "");
    J("    %s %d\n", st_op, slot_jvm(lc_slot)); JGoto(lstore);
    /* right_relay: value on stack → promote to double if needed → drain to rc_slot */
    JL(right_relay);
    if (is_dbl && !ij_expr_is_real(n->children[1])) JI("l2d", "");
    J("    %s %d\n", st_op, slot_jvm(rc_slot)); JGoto(chk);

    JL(lbfwd); JGoto(lb);
    JL(a); JGoto(la);
    JL(b); JGoto(rb);

    /* lstore: lc_slot has left → go to right.α */
    JL(lstore); JGoto(ra);

    /* chk: lc_slot=left, rc_slot=right — stack empty */
    JL(chk);
    J("    %s %d\n", ld_op, slot_jvm(lc_slot));   /* push left */
    J("    %s %d\n", ld_op, slot_jvm(rc_slot));   /* push right */

    const char *jfail;
    if (is_dbl) {
        /* dcmpl: pushes -1 if either NaN, or left < right; 0 if equal; 1 if left > right.
         * Use dcmpl for </<= (NaN → -1 → treated as less, safe fail for > case).
         * Use dcmpg for >/>= (NaN → +1 → treated as greater, safe fail for < case).
         * For = and ~=, dcmpl works (NaN yields -1 or 1, never 0 → correct). */
        switch (n->kind) {
            case ICN_LT: JI("dcmpl",""); jfail = "ifge"; break;
            case ICN_LE: JI("dcmpl",""); jfail = "ifgt"; break;
            case ICN_GT: JI("dcmpg",""); jfail = "ifle"; break;
            case ICN_GE: JI("dcmpg",""); jfail = "iflt"; break;
            case ICN_EQ: JI("dcmpl",""); jfail = "ifne"; break;
            case ICN_NE: JI("dcmpl",""); jfail = "ifeq"; break;
            default:     JI("dcmpl",""); jfail = "ifne"; break;
        }
    } else {
        /* Integer path: lcmp leaves int on stack */
        JI("lcmp","");
        switch (n->kind) {
            case ICN_LT: jfail = "ifge"; break;
            case ICN_LE: jfail = "ifgt"; break;
            case ICN_GT: jfail = "ifle"; break;
            case ICN_GE: jfail = "iflt"; break;
            case ICN_EQ: jfail = "ifne"; break;
            case ICN_NE: jfail = "ifeq"; break;
            default:     jfail = "ifne"; break;
        }
    }
    J("    %s %s\n", jfail, rb);
    /* success: reload right as result value */
    J("    %s %d\n", ld_op, slot_jvm(rc_slot));
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
        char ba[64], bb[64];
        ij_loop_push(ports.ω, ga);   /* break→exit every, next→pump generator */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        IjPorts gp; strncpy(gp.γ,bstart,63); strncpy(gp.ω,ports.ω,63);
        ij_emit_expr(gen, gp, ga, gb);
        JL(bstart); JI(ij_expr_is_string(gen) ? "pop" : "pop2",""); JGoto(ba);
    } else {
        IjPorts gp; strncpy(gp.γ,gbfwd,63); strncpy(gp.ω,ports.ω,63);
        ij_emit_expr(gen, gp, ga, gb);
    }
    /* gbfwd: generator yielded — drain value, kick beta to get next */
    JL(gbfwd); JI(ij_expr_is_string(gen) ? "pop" : "pop2",""); JGoto(gb);
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
        ij_loop_push(ports.ω, ca);   /* break→exit, next→re-eval cond */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        JGoto(ba);
        JL(loop_top); JGoto(ca);
    } else {
        JGoto(ca);
    }
    JL(a); JGoto(ca);
    JL(b); JGoto(ports.ω);
}

static void ij_emit_until(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    /* until E do body end — loop while E FAILS; exit when E SUCCEEDS.
     * Byrd-box dual of while: cond.γ → ports.ω (cond succeeded → stop)
     *                         cond.ω → body.α  (cond failed → run body) */
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *cond = n->children[0];
    IcnNode *body = (n->nchildren > 1) ? n->children[1] : NULL;

    char cond_ok[64];   snprintf(cond_ok,   sizeof cond_ok,   "icn_%d_condok",  id);
    char cond_fail[64]; snprintf(cond_fail, sizeof cond_fail, "icn_%d_cfail",   id);
    char loop_top[64];  snprintf(loop_top,  sizeof loop_top,  "icn_%d_top",     id);

    /* cond.γ → cond_ok (discard value, then exit); cond.ω → cond_fail → body */
    IjPorts cp; strncpy(cp.γ, cond_ok, 63); strncpy(cp.ω, cond_fail, 63);
    char ca[64], cb[64]; ij_emit_expr(cond, cp, ca, cb);

    /* cond succeeded → discard value, jump to ports.ω */
    JL(cond_ok);
    if (!ij_expr_is_string(cond)) { JI("pop2",""); } else { JI("pop",""); }
    JGoto(ports.ω);

    /* cond failed → run body */
    JL(cond_fail);
    if (body) {
        char ba[64], bb[64];
        char body_ok[64]; snprintf(body_ok, sizeof body_ok, "icn_%d_bok", id);
        IjPorts bp; strncpy(bp.γ, body_ok, 63); strncpy(bp.ω, loop_top, 63);
        ij_loop_push(ports.ω, ca);   /* break→exit, next→re-eval cond */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        JGoto(ba);
        /* body succeeded: drain value, loop */
        JL(body_ok);
        if (!ij_expr_is_string(body)) { JI("pop2",""); } else { JI("pop",""); }
        JL(loop_top); JGoto(ca);
    } else {
        JL(loop_top); JGoto(ca);
    }
    JL(a); JGoto(ca);
    JL(b); JGoto(ports.ω);
}

static void ij_emit_repeat(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    /* repeat body end — run body forever; break exits via ports.ω.
     * body.γ and body.ω both loop back to body.α. */
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *body = (n->nchildren > 0) ? n->children[0] : NULL;

    char loop_top[64]; snprintf(loop_top, sizeof loop_top, "icn_%d_top", id);

    if (body) {
        char ba[64], bb[64];
        char body_ok[64]; snprintf(body_ok, sizeof body_ok, "icn_%d_bok", id);
        IjPorts bp; strncpy(bp.γ, body_ok, 63); strncpy(bp.ω, loop_top, 63);
        ij_loop_push(ports.ω, loop_top);  /* break→exit, next→loop_top */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        JL(a); JL(loop_top); JGoto(ba);
        /* body succeeded: drain value, loop again */
        JL(body_ok);
        if (!ij_expr_is_string(body)) { JI("pop2",""); } else { JI("pop",""); }
        JGoto(loop_top);
    } else {
        JL(a); JL(loop_top); JGoto(loop_top);  /* infinite empty loop */
    }
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * String type predicate and concat emitter
 * ======================================================================= */
static int ij_expr_is_string(IcnNode *n) {
    if (!n) return 0;
    switch (n->kind) {
        case ICN_STR:    return 1;
        case ICN_CSET:   return 1;  /* cset literal is a String */
        case ICN_CONCAT: return 1;
        case ICN_LCONCAT:return 1;  /* Tiny-ICON: ||| treated as string concat */
        case ICN_BANG:   return 1;  /* !E yields single-char Strings */
        case ICN_AUGOP:  /* ||:= (TK_AUGCONCAT=35) yields String; arithmetic augops yield long */
            return (n->val.ival == 35) ? 1 : 0;
        case ICN_CALL: {
            if (n->nchildren >= 1) {
                IcnNode *fn = n->children[0];
                if (fn && fn->kind == ICN_VAR) {
                    const char *fn_name = fn->val.sval;
                    /* write(str_arg) returns its argument */
                    if (strcmp(fn_name, "write") == 0 && n->nchildren >= 2)
                        return ij_expr_is_string(n->children[1]);
                    /* tab/move return String substrings */
                    if (strcmp(fn_name, "tab") == 0)    return 1;
                    if (strcmp(fn_name, "move") == 0)   return 1;
                    /* string() conversion returns String */
                    if (strcmp(fn_name, "string") == 0) return 1;
                    /* match returns a long position, not a String */
                }
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
        case ICN_IF: {
            /* if/then/else: result type = then-branch type (conservative: check then) */
            if (n->nchildren >= 2) return ij_expr_is_string(n->children[1]);
            return 0;
        }
        case ICN_ALT: {
            /* Alternation yields string iff children yield strings (check first child) */
            if (n->nchildren >= 1) return ij_expr_is_string(n->children[0]);
            return 0;
        }
        case ICN_LIMIT: {
            /* E \ N yields same type as E */
            if (n->nchildren >= 1) return ij_expr_is_string(n->children[0]);
            return 0;
        }
        case ICN_SWAP: {
            /* :=: returns new lhs value — same type as children */
            if (n->nchildren >= 1) return ij_expr_is_string(n->children[0]);
            return 0;
        }
        case ICN_SUBSCRIPT:
            return 1;  /* s[i] always yields a single-char String */
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
    /* left_is_value: true only for one-shot string producers (literals, vars, one-shot calls).
     * ICN_ALT is a generator — its β must be used to advance it, not restart from α. */
    int left_is_value = ij_expr_is_string(lchild) && lchild->kind != ICN_ALT;

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

/* Re-implement ICN_NOT cleanly with succeed trampoline */
static void ij_emit_not(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    IcnNode *child = (n->nchildren >= 1) ? n->children[0] : NULL;
    char child_ok[64];  snprintf(child_ok,  sizeof child_ok,  "icn_%d_cok",  id);
    char succeed[64];   snprintf(succeed,   sizeof succeed,   "icn_%d_succ", id);
    IjPorts cp;
    strncpy(cp.γ, child_ok, 63);  /* child success → not fails */
    strncpy(cp.ω, succeed,  63);  /* child fail    → not succeeds */
    char ca[64], cb[64];
    ij_emit_expr(child, cp, ca, cb);
    JC("NOT"); JL(a); JGoto(ca);
    JL(b); JGoto(cb);
    JL(child_ok);
    if (ij_expr_is_string(child)) { JI("pop",""); } else { JI("pop2",""); }
    JGoto(ports.ω);
    JL(succeed);
    JI("lconst_0","");   /* push dummy long 0 as result */
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_NEG — unary minus: -E
 * ======================================================================= */
static void ij_emit_neg(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    IcnNode *child = (n->nchildren >= 1) ? n->children[0] : NULL;
    char negate[64]; snprintf(negate, sizeof negate, "icn_%d_neg", id);
    IjPorts cp; strncpy(cp.γ, negate, 63); strncpy(cp.ω, ports.ω, 63);
    char ca[64], cb[64]; ij_emit_expr(child, cp, ca, cb);
    JC("NEG"); JL(a); JGoto(ca); JL(b); JGoto(cb);
    JL(negate);
    if (ij_expr_is_real(child)) JI("dneg",""); else JI("lneg","");
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_TO_BY — E1 to E2 by E3  (stepped range generator)
 * step>0: yield i while i<=end; step<0: yield i while i>=end.
 *
 * Forward-only jump structure (no backward branches -> no StackMapTable):
 *
 *   a:     eval E1 -> I_f; eval E2 -> end_f; eval E3 -> step_f; goto check
 *   b:     I_f += step_f; goto check
 *   check: (reached by forward jumps from both a and b)
 *          step>0: if I_f > end_f -> ports.w; else push I_f -> ports.g
 *          step<0: if I_f < end_f -> ports.w; else push I_f -> ports.g
 *          step=0: -> ports.w
 * ======================================================================= */
static void ij_emit_to_by(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *e1 = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *e2 = (n->nchildren > 1) ? n->children[1] : NULL;
    IcnNode *e3 = (n->nchildren > 2) ? n->children[2] : NULL;

    /* Determine if any operand is real → use double fields throughout */
    int is_dbl = ij_expr_is_real(e1) || ij_expr_is_real(e2) || ij_expr_is_real(e3);

    /* Static fields — D for real, J for integer */
    char I_f[64], end_f[64], step_f[64];
    snprintf(I_f,    sizeof I_f,    "icn_%d_toby_i",   id);
    snprintf(end_f,  sizeof end_f,  "icn_%d_toby_end", id);
    snprintf(step_f, sizeof step_f, "icn_%d_toby_stp", id);
    if (is_dbl) {
        ij_declare_static_real(I_f);
        ij_declare_static_real(end_f);
        ij_declare_static_real(step_f);
    } else {
        ij_declare_static(I_f);
        ij_declare_static(end_f);
        ij_declare_static(step_f);
    }

/* Helpers: get/put the counter field and push zero for comparison */
#define TB_GET_I()    (is_dbl ? ij_get_real_field(I_f)    : ij_get_long(I_f))
#define TB_PUT_I()    (is_dbl ? ij_put_real_field(I_f)    : ij_put_long(I_f))
#define TB_GET_END()  (is_dbl ? ij_get_real_field(end_f)  : ij_get_long(end_f))
#define TB_GET_STP()  (is_dbl ? ij_get_real_field(step_f) : ij_get_long(step_f))
#define TB_PUT_END()  (is_dbl ? ij_put_real_field(end_f)  : ij_put_long(end_f))
#define TB_PUT_STP()  (is_dbl ? ij_put_real_field(step_f) : ij_put_long(step_f))
#define TB_ZERO()     (is_dbl ? JI("dconst_0","")         : JI("lconst_0",""))
#define TB_ADD()      (is_dbl ? JI("dadd","")             : JI("ladd",""))
#define TB_CMP()      (is_dbl ? JI("dcmpl","")            : JI("lcmp",""))
#define TB_CMP_GT()   (is_dbl ? JI("dcmpg","")            : JI("lcmp",""))
/* Promote operand to double if needed (only in dbl mode) */
#define TB_PROMOTE(expr) if (is_dbl && !ij_expr_is_real(expr)) { JI("l2d",""); }

    /* Labels */
    char r1[64], r2[64], r3[64], check[64], chkp[64], chkn[64];
    snprintf(r1,    sizeof r1,    "icn_%d_tb_r1",    id);
    snprintf(r2,    sizeof r2,    "icn_%d_tb_r2",    id);
    snprintf(r3,    sizeof r3,    "icn_%d_tb_r3\n",  id);  /* trailing \n trick: unused */
    snprintf(r3,    sizeof r3,    "icn_%d_tb_r3",    id);
    snprintf(check, sizeof check, "icn_%d_tb_check", id);
    snprintf(chkp,  sizeof chkp,  "icn_%d_tb_ckp",  id);
    snprintf(chkn,  sizeof chkn,  "icn_%d_tb_ckn\n",id);
    snprintf(chkn,  sizeof chkn,  "icn_%d_tb_ckn",  id);

    IjPorts p1; strncpy(p1.γ,r1,63); strncpy(p1.ω,ports.ω,63);
    IjPorts p2; strncpy(p2.γ,r2,63); strncpy(p2.ω,ports.ω,63);
    IjPorts p3; strncpy(p3.γ,r3,63); strncpy(p3.ω,ports.ω,63);

    char a1[64],b1[64],a2[64],b2[64],a3[64],b3[64];
    ij_emit_expr(e1, p1, a1, b1);
    ij_emit_expr(e2, p2, a2, b2);
    ij_emit_expr(e3, p3, a3, b3);

    JC("TO_BY -- forward-only, supports integer and real");

    /* α: eval E1 → r1 */
    JL(a);  JGoto(a1);
    JL(r1); TB_PROMOTE(e1); TB_PUT_I();    JGoto(a2);
    JL(r2); TB_PROMOTE(e2); TB_PUT_END();  JGoto(a3);
    JL(r3); TB_PROMOTE(e3); TB_PUT_STP(); JGoto(check);

    /* β: I += step, → check */
    JL(b);
    TB_GET_I(); TB_GET_STP(); TB_ADD(); TB_PUT_I();
    JGoto(check);

    /* check: forward-only — inspect step sign, then bounds check */
    JL(check);
    TB_GET_STP(); TB_ZERO(); TB_CMP();
    J("    ifgt %s\n", chkp);
    TB_GET_STP(); TB_ZERO(); TB_CMP();
    J("    iflt %s\n", chkn);
    JGoto(ports.ω);   /* step == 0 → always fail */

    JL(chkp);   /* positive step: yield while I <= end */
    TB_GET_I(); TB_GET_END(); TB_CMP_GT();
    J("    ifgt %s\n", ports.ω);
    TB_GET_I(); JGoto(ports.γ);

    JL(chkn);   /* negative step: yield while I >= end */
    TB_GET_I(); TB_GET_END(); TB_CMP();
    J("    iflt %s\n", ports.ω);
    TB_GET_I(); JGoto(ports.γ);

#undef TB_GET_I
#undef TB_PUT_I
#undef TB_GET_END
#undef TB_GET_STP
#undef TB_PUT_END
#undef TB_PUT_STP
#undef TB_ZERO
#undef TB_ADD
#undef TB_CMP
#undef TB_CMP_GT
#undef TB_PROMOTE

    (void)b1; (void)b2; (void)b3;
}

/* =========================================================================
 * String relational ops: ICN_SEQ (==), ICN_SNE (~==), ICN_SLT (<<),
 * ICN_SLE (<<=), ICN_SGT (>>), ICN_SGE (>>=)
 * Use String.compareTo() and compare result to 0.
 * Both operands are String refs on the stack.
 * ======================================================================= */
static void ij_emit_strrelop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    char lrelay[64], rrelay[64], chk[64], lstore_lbl[64];
    snprintf(lrelay,    sizeof lrelay,    "icn_%d_lrelay", id);
    snprintf(rrelay,    sizeof rrelay,    "icn_%d_rrelay", id);
    snprintf(chk,       sizeof chk,       "icn_%d_chk",    id);
    snprintf(lstore_lbl,sizeof lstore_lbl,"icn_%d_lstore", id);
    /* Allocate scratch slots for String refs (astore uses 1 slot each) */
    int ls = ij_locals_alloc_tmp(); /* left String */
    int rs = ij_locals_alloc_tmp(); /* right String */
    IcnNode *lhs = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *rhs = (n->nchildren > 1) ? n->children[1] : NULL;
    IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);
    IjPorts rp; strncpy(rp.γ, rrelay, 63); strncpy(rp.ω, ports.ω, 63);
    char la[64], lb2[64], ra[64], rb2[64];
    ij_emit_expr(lhs, lp, la, lb2);
    ij_emit_expr(rhs, rp, ra, rb2);
    JC("STRRELOP"); JL(a); JGoto(la);
    JL(b); JGoto(lb2);
    JL(lrelay); J("    astore %d\n", slot_jvm(ls)); JGoto(lstore_lbl);
    JL(lstore_lbl); JGoto(ra);
    JL(rrelay); J("    astore %d\n", slot_jvm(rs)); JGoto(chk);
    JL(chk);
    J("    aload %d\n", slot_jvm(ls));
    J("    aload %d\n", slot_jvm(rs));
    JI("invokevirtual", "java/lang/String/compareTo(Ljava/lang/String;)I");
    const char *jfail;
    switch (n->kind) {
        case ICN_SEQ: jfail = "ifne"; break;
        case ICN_SNE: jfail = "ifeq"; break;
        case ICN_SLT: jfail = "ifge"; break;
        case ICN_SLE: jfail = "ifgt"; break;
        case ICN_SGT: jfail = "ifle"; break;
        case ICN_SGE: jfail = "iflt"; break;
        default:      jfail = "ifne"; break;
    }
    J("    %s %s\n", jfail, rb2);
    /* success: reload right String as result, convert to long (dummy 0) */
    /* String relops return the right operand in Icon — but we need a long
     * for consistent stack. Push 0L as dummy result. */
    JI("lconst_0","");
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_BREAK — exit enclosing loop; optional value (ignored in our impl)
 * break jumps directly to the enclosing loop's ports.ω (exit path).
 * ======================================================================= */
static void ij_emit_break(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    const char *exit_lbl = ij_loop_break_target();
    JL(a); JGoto(exit_lbl);
    JL(b); JGoto(exit_lbl);  /* β also exits — break is one-shot */
    (void)ports; (void)n;
}

/* =========================================================================
 * ICN_NEXT — restart enclosing loop body (skip rest of current iteration)
 * next jumps to the loop's restart label (loop_top / cond re-eval).
 * ======================================================================= */
static void ij_emit_next(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    const char *next_lbl = ij_loop_next_target();
    JL(a); JGoto(next_lbl);
    JL(b); JGoto(next_lbl);
    (void)ports; (void)n;
}

/* =========================================================================
 * ICN_AUGOP — augmented assignment: lhs op:= rhs
 *   Semantics: lhs := lhs op rhs  (lhs must be a variable)
 *   node->val.ival encodes the augop token kind (TK_AUGPLUS etc.)
 *   We: load lhs, eval rhs, apply op, store back, push result → ports.γ
 * ======================================================================= */
static void ij_emit_augop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (n->nchildren < 2) {
        int id = ij_new_id(); char a[64], b[64];
        lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
        strncpy(oα,a,63); strncpy(oβ,b,63);
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }

    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *lhs = n->children[0];
    IcnNode *rhs = n->children[1];
    long aug_kind = n->val.ival;

    /* Eval rhs; on rhs fail → ports.ω */
    char rhs_ok[64]; snprintf(rhs_ok, sizeof rhs_ok, "icn_%d_rhsok", id);
    IjPorts rp; strncpy(rp.γ, rhs_ok, 63); strncpy(rp.ω, ports.ω, 63);
    char ra[64], rb[64]; ij_emit_expr(rhs, rp, ra, rb);

    JL(a); JGoto(ra);
    JL(b); JGoto(rb);

    JL(rhs_ok);
    /* rhs value is on stack — String ref for ||:=, long for arithmetic ops.
     * Load lhs current value, apply op, store result back, push result. */
    if (lhs && lhs->kind == ICN_VAR) {
        char fld[128];
        int is_local = (ij_locals_find(lhs->val.sval) >= 0);
        if (is_local) {
            ij_var_field(lhs->val.sval, fld, sizeof fld);
        } else {
            snprintf(fld, sizeof fld, "icn_gvar_%s", lhs->val.sval);
        }

        if ((int)aug_kind == 35) {
            /* TK_AUGCONCAT=35: String ||:= String
             * Stack at rhs_ok: String ref (rhs). */
            char tmp_str_fld[128]; snprintf(tmp_str_fld, sizeof tmp_str_fld, "icn_%d_augtemp_s", id);
            ij_declare_static_str(tmp_str_fld);
            ij_declare_static_str(fld);             /* ensure lhs field is str-typed */
            ij_put_str_field(tmp_str_fld);          /* save rhs String */
            ij_get_str_field(fld);                  /* load current lhs String */
            ij_get_str_field(tmp_str_fld);          /* reload rhs String */
            JI("invokevirtual", "java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;");
            JI("dup","");                           /* one copy stays on stack for γ */
            ij_put_str_field(fld);                  /* store result back to lhs */
            JGoto(ports.γ);
        } else {
            /* Arithmetic augop: rhs is long on stack. */
            char tmp_fld[128]; snprintf(tmp_fld, sizeof tmp_fld, "icn_%d_augtemp", id);
            ij_declare_static(tmp_fld);
            ij_put_long(tmp_fld);              /* save rhs */

            ij_declare_static(fld);
            ij_get_long(fld);                  /* load current lhs value */
            ij_get_long(tmp_fld);              /* reload rhs */

            /* Apply arithmetic op. Actual TK_AUG* values (from icon_lex.h enum):
             * TK_AUGPLUS=30 TK_AUGMINUS=31 TK_AUGSTAR=32 TK_AUGSLASH=33 TK_AUGMOD=34 */
            switch ((int)aug_kind) {
                case 30: JI("ladd",""); break;  /* TK_AUGPLUS   */
                case 31: JI("lsub",""); break;  /* TK_AUGMINUS  */
                case 32: JI("lmul",""); break;  /* TK_AUGSTAR   */
                case 33: JI("ldiv",""); break;  /* TK_AUGSLASH  */
                case 34: JI("lrem",""); break;  /* TK_AUGMOD    */
                default: JI("ladd",""); break;
            }

            /* dup result: one copy stored back to lhs, one stays on stack as expr result */
            JI("dup2","");
            ij_put_long(fld);
            JGoto(ports.γ);
        }
    } else {
        /* Non-var lhs — just discard and push rhs as result (rare/error case) */
        JGoto(ports.γ);
    }
}

/* =========================================================================
 * ICN_BANG — !E  string character generator
 * Byrd-box: α evals E once into static str field, resets pos=0, then falls
 * through to check.  check: if pos >= length → ω.  Else substring(pos,pos+1)
 * on stack → γ; pos incremented before goto γ.  β → check (resume).
 * Result type: String (single char).  ij_expr_is_string returns 1 for BANG.
 * ======================================================================= */
static void ij_emit_bang(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id();
    char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char str_fld[128]; snprintf(str_fld, sizeof str_fld, "icn_%d_bang_str", id);
    char pos_fld[128]; snprintf(pos_fld, sizeof pos_fld, "icn_%d_bang_pos", id);
    char check[64];    snprintf(check,   sizeof check,   "icn_%d_bang_chk", id);
    char advance[64];  snprintf(advance, sizeof advance,  "icn_%d_bang_adv", id);

    ij_declare_static_str(str_fld);
    ij_declare_static_int(pos_fld);

    /* child expr */
    IjPorts ep; strncpy(ep.γ, advance, 63); strncpy(ep.ω, ports.ω, 63);
    char ea[64], eb[64]; ij_emit_expr(n->children[0], ep, ea, eb);

    /* α: eval child, store string, reset pos, goto check */
    JL(a); JGoto(ea);
    /* β: resume — just re-enter check without re-evaluating E */
    JL(b); JGoto(check);

    /* advance: child produced String on stack → store into str_fld, reset pos */
    JL(advance);
    ij_put_str_field(str_fld);
    JI("iconst_0",""); ij_put_int_field(pos_fld);
    /* fall through to check */

    /* check: if pos >= length(str) → ω; else extract char, increment pos, → γ */
    JL(check);
    ij_get_str_field(str_fld);
    JI("invokevirtual","java/lang/String/length()I");
    ij_get_int_field(pos_fld);
    /* stack: [length, pos] — if pos >= length → ω */
    J("    if_icmple %s\n", ports.ω);  /* if length <= pos → ω  (i.e. pos >= length) */

    /* extract substring(pos, pos+1) */
    ij_get_str_field(str_fld);
    ij_get_int_field(pos_fld);
    ij_get_int_field(pos_fld);
    JI("iconst_1","");
    JI("iadd","");
    JI("invokevirtual","java/lang/String/substring(II)Ljava/lang/String;");
    /* increment pos */
    ij_get_int_field(pos_fld);
    JI("iconst_1","");
    JI("iadd","");
    ij_put_int_field(pos_fld);
    /* String result is on stack → γ */
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_SIZE — *E  string size operator
 * One-shot: α evals child (must be String), calls String.length(), converts
 * int→long via i2l, pushes long → ports.γ.  β → ports.ω (one-shot, no retry).
 * ======================================================================= */
static void ij_emit_size(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    char relay[64]; snprintf(relay, sizeof relay, "icn_%d_relay", id);
    IcnNode *child = (n->nchildren > 0) ? n->children[0] : NULL;
    IjPorts cp; strncpy(cp.γ, relay, 63); strncpy(cp.ω, ports.ω, 63);
    char ca[64], cb[64]; ij_emit_expr(child, cp, ca, cb);
    JL(a); JGoto(ca);
    JL(b); JGoto(cb);          /* one-shot: β = re-eval child β = fail */
    /* relay: String ref on stack */
    JL(relay);
    JI("invokevirtual", "java/lang/String/length()I");
    JI("i2l", "");             /* int → long for uniform numeric stack type */
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_LIMIT — E \ N  (limitation operator, JCON-ANALYSIS §"E \ N")
 *
 * Semantics: yield at most N values from E.
 *   - N is evaluated once (bounded) at α → stored in limit_max (long)
 *   - counter starts 0 at α
 *   - each time E yields: if counter >= max → exhaust (goto E.β → ports.ω)
 *                          else counter++; propagate value → ports.γ
 *   - β: if counter >= max → ports.ω; else counter++; → E.β
 *
 * Per-site static fields:
 *   icn_N_limit_count  J  — how many values yielded so far (long)
 *   icn_N_limit_max    J  — N value, evaluated once
 *
 * Children: [0]=E (the generator), [1]=N (the bound)
 * ======================================================================= */
static void ij_emit_limit(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Per-site statics */
    char cnt_fld[64], max_fld[64];
    snprintf(cnt_fld, sizeof cnt_fld, "icn_%d_limit_count", id);
    snprintf(max_fld, sizeof max_fld, "icn_%d_limit_max",   id);
    ij_declare_static(cnt_fld);   /* long */
    ij_declare_static(max_fld);   /* long */

    IcnNode *expr  = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *bound = (n->nchildren > 1) ? n->children[1] : NULL;

    /* Relay labels */
    char n_relay[64]; snprintf(n_relay, sizeof n_relay, "icn_%d_limit_nrelay", id);
    char e_gamma[64]; snprintf(e_gamma, sizeof e_gamma, "icn_%d_limit_egamma", id);
    char chk_ex[64];  snprintf(chk_ex,  sizeof chk_ex,  "icn_%d_limit_chkex",  id);

    /* Emit N (bounded — one-shot, eval once) */
    IjPorts np; strncpy(np.γ, n_relay, 63); strncpy(np.ω, ports.ω, 63);
    char na[64], nb[64]; ij_emit_expr(bound, np, na, nb);

    /* Emit E — γ → e_gamma (we intercept), ω → ports.ω */
    IjPorts ep; strncpy(ep.γ, e_gamma, 63); strncpy(ep.ω, ports.ω, 63);
    char ea[64], eb[64]; ij_emit_expr(expr, ep, ea, eb);

    /* n_relay: N value (long) on stack → store max, reset count=0, goto E.α */
    JL(n_relay);
    ij_put_long(max_fld);
    JI("lconst_0", ""); ij_put_long(cnt_fld);
    JGoto(ea);

    /* e_gamma: E yielded a value (long or String) on stack.
     * Check counter: if count >= max → pop value, goto E.β (exhaust).
     * Else: count++, propagate value to ports.γ.
     * We must not disturb the value on the stack during the check, so we
     * use a relay: store value in a temp static first, check, then reload. */
    char val_fld[64]; snprintf(val_fld, sizeof val_fld, "icn_%d_limit_val", id);
    int expr_is_str = ij_expr_is_string(expr);
    if (expr_is_str) {
        ij_declare_static_str(val_fld);
    } else {
        ij_declare_static(val_fld);
    }
    JL(e_gamma);
    /* store value */
    if (expr_is_str) { ij_put_str_field(val_fld); }
    else             { ij_put_long(val_fld); }

    /* check: count >= max? */
    JL(chk_ex);
    ij_get_long(cnt_fld);
    ij_get_long(max_fld);
    JI("lcmp", "");
    J("    ifge %s\n", eb);       /* count >= max → exhaust via E.β */

    /* count++ */
    ij_get_long(cnt_fld);
    JI("lconst_1", ""); JI("ladd", "");
    ij_put_long(cnt_fld);

    /* reload value and succeed */
    if (expr_is_str) { ij_get_str_field(val_fld); }
    else             { ij_get_long(val_fld); }
    JGoto(ports.γ);

    /* α: eval N first (to get max), then will fall through to E.α via n_relay */
    JL(a); JGoto(na);

    /* β: check counter; if exhausted → ports.ω; else resume E.β
     * Do NOT increment here — counter only increments when a value is actually
     * yielded (in e_gamma path).  β just checks and re-drives the inner generator. */
    JL(b);
    ij_get_long(cnt_fld);
    ij_get_long(max_fld);
    JI("lcmp", "");
    J("    ifge %s\n", ports.ω);  /* count >= max: already gave all allowed values */
    JGoto(eb);
}

/* =========================================================================
 * ICN_SUBSCRIPT — s[i]  (string character subscript, 1-based)
 *
 * Semantics: s[i] returns the single character at 1-based position i as a
 * String.  Fails if i < 1 or i > length(s).
 * Negative indices count from the end: s[-1] = last char.
 *
 * Implementation (one-shot — β → ω):
 *   eval s → store in per-site static (String)
 *   eval i → l2i → compute 0-based offset (handle negative)
 *   bounds check: if offset < 0 || offset >= length → ω
 *   substring(offset, offset+1) → γ (String)
 *
 * Children: [0]=string expr, [1]=index expr (long, 1-based)
 * Result type: String (single character)
 * ======================================================================= */
static void ij_emit_subscript(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 2) {
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }

    IcnNode *str_child = n->children[0];
    IcnNode *idx_child = n->children[1];

    /* Per-site statics for cross-label value passing */
    char s_fld[64], i_fld[64];
    snprintf(s_fld, sizeof s_fld, "icn_%d_sub_s", id);
    snprintf(i_fld, sizeof i_fld, "icn_%d_sub_i", id);
    ij_declare_static_str(s_fld);
    ij_declare_static_int(i_fld);   /* 0-based int offset */

    /* Relay labels */
    char s_relay[64], i_relay[64], check[64];
    snprintf(s_relay, sizeof s_relay, "icn_%d_sub_srelay", id);
    snprintf(i_relay, sizeof i_relay, "icn_%d_sub_irelay", id);
    snprintf(check,   sizeof check,   "icn_%d_sub_check",  id);

    /* Emit index child (long) — fail→ω */
    IjPorts ip; strncpy(ip.γ, i_relay, 63); strncpy(ip.ω, ports.ω, 63);
    char ia[64], ib[64]; ij_emit_expr(idx_child, ip, ia, ib);

    /* Emit string child — fail→ω */
    IjPorts sp; strncpy(sp.γ, s_relay, 63); strncpy(sp.ω, ports.ω, 63);
    char sa[64], sb[64]; ij_emit_expr(str_child, sp, sa, sb);

    /* s_relay: String on stack → store, then eval index */
    JL(s_relay);
    ij_put_str_field(s_fld);
    JGoto(ia);

    /* i_relay: long index on stack → convert to 0-based int, store, goto check */
    JL(i_relay);
    JI("l2i", "");                       /* long → int (1-based Icon index) */
    /* Convert 1-based to 0-based, handling negative (count from end) */
    /* if i > 0: offset = i - 1
       if i < 0: offset = length + i   (e.g. s[-1] = length-1)
       if i == 0: fail */
    char pos_branch[64], neg_branch[64], zero_fail[64];
    snprintf(pos_branch, sizeof pos_branch, "icn_%d_sub_pos", id);
    snprintf(neg_branch, sizeof neg_branch, "icn_%d_sub_neg", id);
    snprintf(zero_fail,  sizeof zero_fail,  "icn_%d_sub_z",   id);
    /* Duplicate int on stack for the comparison (it's on stack once) */
    int idx_slot = ij_locals_alloc_tmp();
    J("    istore %d\n", slot_jvm(idx_slot));
    J("    iload %d\n",  slot_jvm(idx_slot));
    J("    ifgt %s\n", pos_branch);
    J("    iload %d\n",  slot_jvm(idx_slot));
    J("    iflt %s\n", neg_branch);
    /* i == 0 → fail */
    JL(zero_fail); JGoto(ports.ω);

    /* pos_branch: offset = i - 1 */
    JL(pos_branch);
    J("    iload %d\n", slot_jvm(idx_slot));
    JI("iconst_1", ""); JI("isub", "");
    ij_put_int_field(i_fld);
    JGoto(check);

    /* neg_branch: offset = length + i */
    JL(neg_branch);
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    J("    iload %d\n", slot_jvm(idx_slot));
    JI("iadd", "");
    ij_put_int_field(i_fld);
    JGoto(check);

    /* check: if offset < 0 || offset >= length → fail; else substring */
    JL(check);
    ij_get_int_field(i_fld);
    J("    iflt %s\n", ports.ω);          /* offset < 0 → fail */
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    ij_get_int_field(i_fld);
    JI("if_icmple", ports.ω);             /* length <= offset → fail */
    /* substring(offset, offset+1) */
    ij_get_str_field(s_fld);
    ij_get_int_field(i_fld);
    ij_get_int_field(i_fld);
    JI("iconst_1", ""); JI("iadd", "");
    JI("invokevirtual", "java/lang/String/substring(II)Ljava/lang/String;");
    JGoto(ports.γ);

    /* α: eval string first, then index */
    JL(a); JGoto(sa);
    /* β: retry index generator (allows every s[1 to N] to work).
     * String child is one-shot and its value is cached in s_fld, so we
     * just re-drive the index child's β to get the next index. */
    JL(b); JGoto(ib);
}


/* =========================================================================
 * ICN_SWAP — E1 :=: E2  (swap values of two variables)
 * Semantics: atomically exchange values of lhs and rhs variables.
 * Returns the new value of E1 (Icon :=: returns lhs after swap).
 * Implementation: read both, write both crossed, using per-site temp statics.
 * Only handles VAR := VAR (the common case); other forms fall to UNIMPL.
 * ======================================================================= */
static void ij_emit_swap(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 2 ||
        !n->children[0] || n->children[0]->kind != ICN_VAR ||
        !n->children[1] || n->children[1]->kind != ICN_VAR) {
        /* Degenerate/unsupported form — fail */
        JL(a); JGoto(ports.ω);
        JL(b); JGoto(ports.ω);
        return;
    }

    IcnNode *lv = n->children[0];
    IcnNode *rv = n->children[1];

    /* Determine field names for both sides */
    char lfld[128], rfld[128];
    int lslot = ij_locals_find(lv->val.sval);
    int rslot = ij_locals_find(rv->val.sval);
    if (lslot >= 0) ij_var_field(lv->val.sval, lfld, sizeof lfld);
    else            snprintf(lfld, sizeof lfld, "icn_gvar_%s", lv->val.sval);
    if (rslot >= 0) ij_var_field(rv->val.sval, rfld, sizeof rfld);
    else            snprintf(rfld, sizeof rfld, "icn_gvar_%s", rv->val.sval);

    /* Determine types from statics table — default long */
    int l_str = 0, l_dbl = 0, r_str = 0, r_dbl = 0;
    for (int i = 0; i < ij_nstatics; i++) {
        if (!strcmp(ij_statics[i], lfld)) {
            l_str = (ij_static_types[i] == 'A');
            l_dbl = (ij_static_types[i] == 'D');
        }
        if (!strcmp(ij_statics[i], rfld)) {
            r_str = (ij_static_types[i] == 'A');
            r_dbl = (ij_static_types[i] == 'D');
        }
    }
    /* Treat both as same type (swap between mismatched types is unusual) */
    int is_str = l_str || r_str;
    int is_dbl = l_dbl || r_dbl;

    /* Declare tmp fields for the swap */
    char tmp1[64], tmp2[64];
    snprintf(tmp1, sizeof tmp1, "icn_%d_swap_t1", id);
    snprintf(tmp2, sizeof tmp2, "icn_%d_swap_t2", id);
    if (is_str)      { ij_declare_static_str(tmp1); ij_declare_static_str(tmp2); }
    else if (is_dbl) { ij_declare_static_dbl(tmp1); ij_declare_static_dbl(tmp2); }
    else             { ij_declare_static(tmp1);     ij_declare_static(tmp2); }

    JL(a);
    /* Read both values into tmps */
    if (is_str)      { ij_get_str_field(lfld); ij_put_str_field(tmp1);
                       ij_get_str_field(rfld); ij_put_str_field(tmp2); }
    else if (is_dbl) { ij_get_dbl(lfld); ij_put_dbl(tmp1);
                       ij_get_dbl(rfld); ij_put_dbl(tmp2); }
    else             { ij_get_long(lfld); ij_put_long(tmp1);
                       ij_get_long(rfld); ij_put_long(tmp2); }
    /* Write crossed */
    if (is_str)      { ij_get_str_field(tmp2); ij_put_str_field(lfld);
                       ij_get_str_field(tmp1); ij_put_str_field(rfld); }
    else if (is_dbl) { ij_get_dbl(tmp2); ij_put_dbl(lfld);
                       ij_get_dbl(tmp1); ij_put_dbl(rfld); }
    else             { ij_get_long(tmp2); ij_put_long(lfld);
                       ij_get_long(tmp1); ij_put_long(rfld); }
    /* Return new value of lhs (tmp2 = old rhs = new lhs) */
    if (is_str)      ij_get_str_field(lfld);
    else if (is_dbl) ij_get_dbl(lfld);
    else             ij_get_long(lfld);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * Dispatch
 * ======================================================================= */
static void ij_emit_expr(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (!n) { ij_emit_fail_node(NULL,ports,oα,oβ); return; }
    switch (n->kind) {
        case ICN_INT:     ij_emit_int      (n,ports,oα,oβ); break;
        case ICN_REAL:    ij_emit_real     (n,ports,oα,oβ); break;
        case ICN_STR:     ij_emit_str      (n,ports,oα,oβ); break;
        case ICN_CSET:    ij_emit_str      (n,ports,oα,oβ); break; /* cset = typed String */
        case ICN_VAR:     ij_emit_var      (n,ports,oα,oβ); break;
        case ICN_ASSIGN:  ij_emit_assign   (n,ports,oα,oβ); break;
        case ICN_RETURN:  ij_emit_return   (n,ports,oα,oβ); break;
        case ICN_SUSPEND: ij_emit_suspend  (n,ports,oα,oβ); break;
        case ICN_FAIL:    ij_emit_fail_node(n,ports,oα,oβ); break;
        case ICN_IF:      ij_emit_if       (n,ports,oα,oβ); break;
        case ICN_ALT:     ij_emit_alt      (n,ports,oα,oβ); break;
        case ICN_AND: {
            /* n-ary conjunction: E1 & E2 & ... & En
             * irgen.icn ir_conjunction: Ei.γ → E(i+1).α; Ei.ω → E(i-1).β; β → En.β
             *
             * Bug fix: must emit LEFT-TO-RIGHT so ccb[i-1] is known when wiring
             * E[i].ω.  But Ei.γ needs to point to E(i+1).α which isn't known yet.
             * Solution: pre-allocate a unique "relay" label for each child's γ,
             * emit left-to-right using those relay labels as γ targets, then emit
             * the relay → real-α trampolines after all children.
             */
            int nc = n->nchildren;
            int cid = ij_new_id(); char ca2[64], cb2[64];
            lbl_α(cid,ca2,sizeof ca2); lbl_β(cid,cb2,sizeof cb2);
            strncpy(oα,ca2,63); strncpy(oβ,cb2,63);
            char (*cca)[64] = malloc(nc * 64);
            char (*ccb)[64] = malloc(nc * 64);
            /* Pre-allocate relay labels for each γ port */
            char (*relay_g)[64] = malloc(nc * 64);
            for (int i = 0; i < nc; i++) {
                snprintf(relay_g[i], 64, "icn_%d_and_rg_%d", cid, i);
                cca[i][0] = '\0'; ccb[i][0] = '\0';
            }
            /* Emit children left-to-right */
            for (int i = 0; i < nc; i++) {
                IjPorts ep;
                /* γ: last child → ports.γ; otherwise relay_g[i] (trampoline) */
                strncpy(ep.γ, (i == nc-1) ? ports.γ : relay_g[i], 63);
                /* ω: first child → ports.ω; otherwise ccb[i-1] (already populated) */
                strncpy(ep.ω, (i == 0) ? ports.ω : ccb[i-1], 63);
                ij_emit_expr(n->children[i], ep, cca[i], ccb[i]);
            }
            /* Emit relay trampolines: relay_g[i] → pop result → cca[i+1]
             * E[i].γ leaves a value on the JVM stack; E[i+1].α expects empty stack.
             * Drain: String result = pop (1 slot); long result = pop2 (2 slots). */
            for (int i = 0; i < nc-1; i++) {
                JL(relay_g[i]);
                int child_is_str = ij_expr_is_string(n->children[i]);
                JI(child_is_str ? "pop" : "pop2", "");
                JGoto(cca[i+1]);
            }
            JL(ca2); JGoto(cca[0]);
            JL(cb2); JGoto(ccb[nc-1]);
            free(cca); free(ccb); free(relay_g);
            break;
        }
        case ICN_ADD: case ICN_SUB: case ICN_MUL: case ICN_DIV: case ICN_MOD:
                          ij_emit_binop    (n,ports,oα,oβ); break;
        case ICN_POW:     ij_emit_pow      (n,ports,oα,oβ); break;
        case ICN_CONCAT:  ij_emit_concat   (n,ports,oα,oβ); break;
        case ICN_LCONCAT: ij_emit_concat   (n,ports,oα,oβ); break; /* Tiny-ICON: ||| = || */
        case ICN_SWAP:    ij_emit_swap      (n,ports,oα,oβ); break;
        case ICN_SUBSCRIPT: ij_emit_subscript(n,ports,oα,oβ); break;
        case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE: case ICN_EQ: case ICN_NE:
                          ij_emit_relop    (n,ports,oα,oβ); break;
        case ICN_TO:      ij_emit_to       (n,ports,oα,oβ); break;
        case ICN_TO_BY:   ij_emit_to_by    (n,ports,oα,oβ); break;
        case ICN_EVERY:   ij_emit_every    (n,ports,oα,oβ); break;
        case ICN_WHILE:   ij_emit_while    (n,ports,oα,oβ); break;
        case ICN_UNTIL:   ij_emit_until    (n,ports,oα,oβ); break;
        case ICN_REPEAT:  ij_emit_repeat   (n,ports,oα,oβ); break;
        case ICN_CALL:    ij_emit_call     (n,ports,oα,oβ); break;
        case ICN_SCAN:    ij_emit_scan     (n,ports,oα,oβ); break;
        case ICN_NOT:     ij_emit_not      (n,ports,oα,oβ); break;
        case ICN_NEG:     ij_emit_neg      (n,ports,oα,oβ); break;
        case ICN_BREAK:   ij_emit_break    (n,ports,oα,oβ); break;
        case ICN_NEXT:    ij_emit_next     (n,ports,oα,oβ); break;
        case ICN_AUGOP:   ij_emit_augop    (n,ports,oα,oβ); break;
        case ICN_BANG:    ij_emit_bang     (n,ports,oα,oβ); break;
        case ICN_SIZE:    ij_emit_size     (n,ports,oα,oβ); break;
        case ICN_LIMIT:   ij_emit_limit    (n,ports,oα,oβ); break;
        case ICN_SEQ: case ICN_SNE: case ICN_SLT:
        case ICN_SLE: case ICN_SGT: case ICN_SGE:
                          ij_emit_strrelop (n,ports,oα,oβ); break;
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

    /* Pre-pass (forward): register string-typed and double-typed variable fields so
     * ij_expr_is_string / ij_expr_is_real work correctly when the reverse emit loop
     * checks statement result types.
     * Walk assignments left-to-right: declare the LHS var's static field type
     * before emit begins. */
    for (int si = 0; si < nstmts; si++) {
        IcnNode *stmt = proc->children[body_start + si];
        if (!stmt || stmt->kind != ICN_ASSIGN || stmt->nchildren < 2) continue;
        IcnNode *lhs = stmt->children[0];
        IcnNode *rhs = stmt->children[1];
        if (!lhs || lhs->kind != ICN_VAR) continue;
        int slot = ij_locals_find(lhs->val.sval);
        char fld[128];
        if (slot >= 0) ij_var_field(lhs->val.sval, fld, sizeof fld);
        else           snprintf(fld, sizeof fld, "icn_gvar_%s", lhs->val.sval);
        if (ij_expr_is_string(rhs)) {
            ij_declare_static_str(fld);
        } else if (ij_expr_is_real(rhs)) {
            ij_declare_static_dbl(fld);
        }
        /* long-typed vars are declared on first use — no pre-declaration needed */
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
        /* ICN_EVERY / ICN_WHILE / ICN_UNTIL / ICN_REPEAT never fire ports.γ with a value —
         * they always end via ports.ω (exhaustion/failure). Skip the drain to avoid
         * JVM VerifyError on dead pop2 with empty stack. */
        if (stmt->kind == ICN_EVERY || stmt->kind == ICN_WHILE ||
            stmt->kind == ICN_UNTIL || stmt->kind == ICN_REPEAT) {
            IjPorts sp; strncpy(sp.γ, next_a, 63); strncpy(sp.ω, next_a, 63);
            char sa[64], sb[64]; ij_emit_expr(stmt, sp, sa, sb);
            strncpy(alphas[i], sa, 63);
            strncpy(next_a, sa, 63);
            continue;
        }
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
    J(".bytecode 45.0\n");   /* Java 6 — no StackMapTable required */
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

    /* Emit built-in scan helper methods (only if cset/scan builtins are used).
     * icn_builtin_any(cs, subj, pos)      → long newpos (1-based) or -1L
     * icn_builtin_many(cs, subj, pos)     → long newpos (1-based) or -1L
     * icn_builtin_upto_step(cs, subj, pos)→ long matched-pos (1-based) or -1L
     *   upto_step scans forward from pos until it finds a char in cs,
     *   returns that 1-based position (caller sets icn_pos=result for next resume). */
    int need_scan_builtins = 0;
    for (int i = 0; i < ij_nstatics; i++)
        if (!strncmp(ij_statics[i], "icn_upto_cs_", 12)) { need_scan_builtins = 1; break; }
    for (int i = 0; i < ij_nstatics; i++)
        if (!strcmp(ij_statics[i], "icn_subject")) { need_scan_builtins = 1; break; }
    /* Also detect find usage (it uses icn_find_s1_N statics) */
    for (int i = 0; i < ij_nstatics; i++)
        if (!strncmp(ij_statics[i], "icn_find_s1_", 12)) { need_scan_builtins = 1; break; }
    if (need_scan_builtins) {
        /* icn_builtin_any(String cs, String subj, int pos) → long */
        J(".method public static icn_builtin_any(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 3\n");
        J("    ; if pos >= subj.length() → return -1\n");
        J("    aload_1\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    iload_2\n");
        J("    if_icmpgt icn_any_inbounds\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_any_inbounds:\n");
        J("    ; cs.indexOf(subj.charAt(pos)) >= 0?\n");
        J("    aload_0\n");
        J("    aload_1\n    iload_2\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_any_match\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_any_match:\n");
        J("    ; return pos+2 (new 1-based pos after consuming one char)\n");
        J("    iload_2\n    iconst_2\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_many(String cs, String subj, int pos) → long */
        J(".method public static icn_builtin_many(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    ; check first char in cs — must match at least one\n");
        J("    aload_1\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    iload_2\n");
        J("    if_icmpgt icn_many_first_ok\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_many_first_ok:\n");
        J("    aload_0\n    aload_1\n    iload_2\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_many_loop_init\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_many_loop_init:\n");
        J("    ; local 3 = pos (working)\n");
        J("    iload_2\n    istore_3\n");
        J("icn_many_loop:\n");
        J("    iinc 3 1\n");
        J("    ; if pos3 >= length → done\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpgt icn_many_cont\n");
        J("    iload_3\n    i2l\n    lconst_1\n    ladd\n    lreturn\n");
        J("icn_many_cont:\n");
        J("    aload_0\n    aload_1\n    iload_3\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_many_loop\n");
        J("    ; stopped — return pos3+1 (1-based)\n");
        J("    iload_3\n    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_upto_step(String cs, String subj, int pos) → long */
        J(".method public static icn_builtin_upto_step(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    iload_2\n    istore_3\n");
        J("icn_upto_scan:\n");
        J("    ; if pos3 >= length → -1\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpgt icn_upto_inbounds\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_upto_inbounds:\n");
        J("    aload_0\n    aload_1\n    iload_3\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_upto_found\n");
        J("    iinc 3 1\n    goto icn_upto_scan\n");
        J("icn_upto_found:\n");
        J("    ; return pos3+1 (1-based); caller sets icn_pos=result for next resume\n");
        J("    iload_3\n    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_find(String s1, String s2, int startpos) → long
         * Returns 1-based index of s1 in s2 starting from startpos (0-based), or -1L. */
        J(".method public static icn_builtin_find(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 3\n");
        J("    aload_1\n    aload_0\n    iload_2\n");
        J("    invokevirtual java/lang/String/indexOf(Ljava/lang/String;I)I\n");
        J("    dup\n    ifge icn_find_ok\n");
        J("    pop\n    ldc2_w -1\n    lreturn\n");
        J("icn_find_ok:\n");
        J("    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_match(String s1, String subj, int pos) → long
         * If subj starts with s1 at pos, return pos+len(s1)+1 as 1-based new pos; else -1L. */
        J(".method public static icn_builtin_match(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 3\n");
        J("    aload_1\n    aload_0\n    iload_2\n");
        J("    invokevirtual java/lang/String/startsWith(Ljava/lang/String;I)Z\n");
        J("    ifne icn_match_ok\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_match_ok:\n");
        J("    iload_2\n    aload_0\n    invokevirtual java/lang/String/length()I\n    iadd\n");
        J("    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_tab_str(int n, String subj, int pos) → String or null
         * n is 1-based target position.  end = n-1 (0-based).
         * Returns subj.substring(pos, end) and sets icn_pos = end; null on bounds failure. */
        J(".method public static icn_builtin_tab_str(ILjava/lang/String;I)Ljava/lang/String;\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    ; end = n-1 (0-based)\n");
        J("    iload_0\n    iconst_1\n    isub\n    istore_3\n");
        J("    ; fail if end < pos\n");
        J("    iload_3\n    iload_2\n    if_icmpge icn_tab_posok\n");
        J("    aconst_null\n    areturn\n");
        J("icn_tab_posok:\n");
        J("    ; fail if end > subj.length()\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpge icn_tab_lenok\n");
        J("    aconst_null\n    areturn\n");
        J("icn_tab_lenok:\n");
        J("    ; update icn_pos = end (static field)\n");
        J("    iload_3\n");
        J("    putstatic %s/icn_pos I\n", ij_classname);
        J("    ; return subj.substring(pos, end)\n");
        J("    aload_1\n    iload_2\n    iload_3\n");
        J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* icn_builtin_move_str(int n, String subj, int pos) → String or null
         * Returns subj.substring(pos, pos+n) and sets icn_pos = pos+n; null if out of bounds. */
        J(".method public static icn_builtin_move_str(ILjava/lang/String;I)Ljava/lang/String;\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    ; end = pos+n\n");
        J("    iload_2\n    iload_0\n    iadd\n    istore_3\n");
        J("    ; fail if end > subj.length()\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpge icn_move_ok\n");
        J("    aconst_null\n    areturn\n");
        J("icn_move_ok:\n");
        J("    ; update icn_pos = end\n");
        J("    iload_3\n");
        J("    putstatic %s/icn_pos I\n", ij_classname);
        J("    ; return subj.substring(pos, end)\n");
        J("    aload_1\n    iload_2\n    iload_3\n");
        J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
        J("    areturn\n");
        J(".end method\n\n");
    }

    /* Emit all procedure methods */
    fputs(procs_text, jout);
    free(procs_text);
    jout = save;
}
