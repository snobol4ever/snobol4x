/*
 * emit_byrd_c.c — SNOBOL4→C emitter for scrip-cc
 *
 * Merged from emit.c + emit_byrd.c.
 * Peer to emit_byrd_asm.c, emit_byrd_jvm.c, emit_byrd_net.c.
 *
 * Emits labeled-goto C — the same Byrd box α/β/γ/ω four-port model
 * as the ASM backend, just with C syntax instead of NASM.
 *
 * C-specific additions vs other backends:
 *   - decl_buf / decl_flush(): two-pass local var declaration (C99
 *     jump-over-declaration rule — locals must precede first goto)
 *   - emit_cnode.h: expression pretty-printer (PP_EXPR/PP_PAT macros)
 *   - trampoline mode (-trampoline flag): stmt-function dispatch table
 *   - NamedPat carries typename/fnname for C struct typedef emission
 *   - FnDef carries body_starts[]/define_stmt for complex body detection
 *
 * Entry point: c_emit(Program*, FILE*)
 */

#define EMIT_BYRD_C_C
#include "scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "emit_cnode.h"

/* -----------------------------------------------------------------------
 * Output
 * ----------------------------------------------------------------------- */

static FILE *out;

static void C(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
}
/* B() — alias used by pattern emitters (emit_byrd.c heritage) */
#define B(...) C(__VA_ARGS__)

/* Three-column pretty printer */
#define PRETTY_OUT out
#include "emit_pretty.h"

/* -----------------------------------------------------------------------
 * UID counter — single counter shared across both pattern and stmt emit
 * ----------------------------------------------------------------------- */

static int uid_ctr = 0;
static int next_uid(void) { return ++uid_ctr; }

/* -----------------------------------------------------------------------
 * C-safe name for a SNOBOL4 variable
 * Prepends '_', replaces non-alnum/_ with '_'.
 * cs()      — static buffer (use before next call)
 * cs_alloc()— heap-allocated (safe across multiple calls)
 * ----------------------------------------------------------------------- */

static const char *cs(const char *s) {
    static char buf[520];
    int i = 0, j = 0;
    buf[j++] = '_';
    for (; s[i] && j < 510; i++) {
        unsigned char c = (unsigned char)s[i];
        buf[j++] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    buf[j] = '\0';
    return buf;
}

/* cs_alloc and cs_label defined below (emit.c versions with lreg collision detection) */

/* Forward declarations */
static int is_io_name(const char *name);
int is_defined_function(const char *name);
static void emit_assign_target(EXPR_t *lhs, const char *rhs_str);
static void emit_assign_target_io(EXPR_t *lhs, const char *rhs_str);
static void emit_expr(EXPR_t *e);
static const char *cs_label(const char *s);
static int is_body_boundary(const char *label, const char *fn);

/* escape_string — emit a C string literal (replaces emit_cstr) */
static void escape_string(const char *s) {
    C("\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if      (c == '"')  C("\\\"");
        else if (c == '\\') C("\\\\");
        else if (c == '\n') C("\\n");
        else if (c == '\r') C("\\r");
        else if (c == '\t') C("\\t");
        else if (c < 0x20 || c > 0x7E) C("\\x%02x", c);
        else C("%c", c);
    }
    C("\"");
}

/* -----------------------------------------------------------------------
 * CNode pretty-printer macros (C-specific expression formatting)
 * ----------------------------------------------------------------------- */

#define PP_EXPR(e, col) do { \
    CArena *_a = cn_arena_new(65536); \
    pp_cnode(build_expr(_a, (e)), out, (col), 4, 120); \
    cn_arena_free(_a); \
} while(0)

#define PP_PAT(e, col) do { \
    CArena *_a = cn_arena_new(65536); \
    pp_cnode(build_pat(_a, (e)), out, (col), 4, 120); \
    cn_arena_free(_a); \
} while(0)

/* -----------------------------------------------------------------------
 * Trampoline mode (-trampoline flag)
 * When 1: each stmt is a void* function, goto → return block_ptr
 * ----------------------------------------------------------------------- */
int trampoline_mode = 0;
static int cur_stmt_next_uid = 0;

/* -----------------------------------------------------------------------
 * Variable registry — collect all SNOBOL4 variable names
 * ----------------------------------------------------------------------- */

#define MAX_VARS 4096
static char *vars[MAX_VARS];
static int   nvar = 0;

static const char *io_names[] = {
    "OUTPUT","INPUT","PUNCH","TERMINAL","TRACE",NULL
};

static int is_io_name(const char *name) {
    for (int i = 0; io_names[i]; i++)
        if (strcasecmp(name, io_names[i]) == 0) return 1;
    return 0;
}

static void var_register(const char *name) {
    if (!name || !*name) return;
    if (is_io_name(name)) return;
    for (int i = 0; i < nvar; i++)
        if (strcmp(vars[i], name) == 0) return;
    if (nvar < MAX_VARS)
        vars[nvar++] = strdup(name);
}

static void collect_expr_vars(EXPR_t *e) {
    if (!e) return;
    switch (e->kind) {
    case E_VAR: var_register(e->sval); break;
    case E_IDX:  var_register(e->sval); break;
    default: break;
    }
    for (int i = 0; i < e->nchildren; i++) collect_expr_vars(e->children[i]);
}

static void collect_vars(Program *prog) {
    nvar = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        collect_expr_vars(s->subject);
        collect_expr_vars(s->pattern);
        collect_expr_vars(s->replacement);
    }
}

/* -----------------------------------------------------------------------
 * FnDef — user-defined function table
 * C-specific: body_starts[] + define_stmt for complex body detection
 * ----------------------------------------------------------------------- */

#define FN_MAX   256
#define FN_ARGMAX 32
#define ARG_MAX FN_ARGMAX  /* alias used in collect_fndefs/parse_proto */
#define BODY_MAX   8

typedef struct {
    char   *name;
    char   *args[FN_ARGMAX];
    int     nargs;
    char   *locals[FN_ARGMAX];
    int     nlocals;
    STMT_t *body_starts[BODY_MAX];
    int     nbody_starts;
    STMT_t *define_stmt;
    char   *end_label;
    char   *entry_label;
} FnDef;

static FnDef fn_table[FN_MAX];
static int   fn_count = 0;
static FnDef *cur_fn_def = NULL;

int is_defined_function(const char *name) {
    static const char *std[] = {
        "APPLY","ARG","ARRAY","ATAN","BACKSPACE","BREAK","BREAKX",
        "CHAR","CHOP","CLEAR","CODE","COLLECT","CONVERT","COPY","COS",
        "DATA","DATATYPE","DATE","DEFINE","DETACH","DIFFER","DUMP","DUPL",
        "EJECT","ENDFILE","EQ","EVAL","EXIT","EXP","FENCE","FIELD",
        "GE","GT","HOST","IDENT","INPUT","INTEGER","ITEM",
        "LE","LEN","LEQ","LGE","LGT","LLE","LLT","LN","LNE","LOAD",
        "LOCAL","LPAD","LT","NE","NOTANY","OPSYN","OUTPUT",
        "POS","PROTOTYPE","REMDR","REPLACE","REVERSE","REWIND","RPAD",
        "RPOS","RSORT","RTAB","SET","SETEXIT","SIN","SIZE","SORT","SPAN",
        "SQRT","STOPTR","SUBSTR","TAB","TRACE","TRIM","UNLOAD","UCASE","LCASE",
        "ANY","ARB","ARBNO","BAL","DT_FAIL","ABORT","REM","SUCCEED",
        "ICASE","UCASE","LCASE","REVERSE","REPLACE","DUPL","LPAD","RPAD",
        NULL
    };
    for (int i = 0; std[i]; i++)
        if (strcasecmp(name, std[i]) == 0) return 1;
    for (int i = 0; i < fn_count; i++)
        if (strcasecmp(fn_table[i].name, name) == 0) return 1;
    return 0;
}

static void emit_computed_goto_inline(const char *label, const char *fn) {
    const char *expr_src = (strncasecmp(label,"$COMPUTED",9)==0 && label[9]==':')
                           ? label+10 : NULL;
    if (!expr_src || !*expr_src || !fn) {
        C("goto _SNO_NEXT_%d", cur_stmt_next_uid);
        return;
    }
    char expr_buf[4096];
    strncpy(expr_buf, expr_src, sizeof(expr_buf)-1);
    expr_buf[sizeof(expr_buf)-1] = '\0';
    int elen = (int)strlen(expr_buf);
    while (elen > 0 && (expr_buf[elen-1] == ')' || expr_buf[elen-1] == ' ' || expr_buf[elen-1] == '\t'))
        expr_buf[--elen] = '\0';
    EXPR_t *ce = parse_expr_from_str(expr_buf);
    if (!ce) { C("goto _SNO_NEXT_%d", cur_stmt_next_uid); return; }
    C("{ const char *_cg_raw = VARVAL_fn(");
    emit_expr(ce);
    C("); char _cg_buf[512]; size_t _cg_j=0;");
    C(" if(_cg_raw) { for(size_t _cg_i=0;_cg_raw[_cg_i]&&_cg_j<sizeof(_cg_buf)-1;_cg_i++)");
    C(" { if(_cg_raw[_cg_i]=='\\'' || _cg_raw[_cg_i]=='\"') continue; _cg_buf[_cg_j++]=_cg_raw[_cg_i]; } }");
    C(" _cg_buf[_cg_j]='\\0'; const char *_cg=_cg_buf;");
    C(" if(0){}");
    for (int i = 0; i < fn_count; i++) {
        if (strcasecmp(fn_table[i].name, fn) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            STMT_t *bs = fn_table[i].body_starts[b];
            for (STMT_t *t = bs; t; t = t->next) {
                if (t->is_end) break;
                if (t != bs && is_body_boundary(t->label, fn_table[i].name)) break;
                if (t->label)
                    C(" else if(strcasecmp(_cg,\"%s\")==0) goto _L%s;",
                      t->label, cs_label(t->label));
            }
        }
        break;
    }
    C(" (void)_cg; }");
}

#define LBUF 320
typedef char Label[LBUF];

static void label_fmt(Label out_l, const char *role, int uid, const char *port) {
    snprintf(out_l, LBUF, "%s_%d_%s", role, uid, port);
}

/* -----------------------------------------------------------------------
 * Forward declaration — byrd_emit is mutually recursive
 * ----------------------------------------------------------------------- */

static void emit_pat_node(EXPR_t *pat,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj, const char *subj_len,
                      const char *cursor,
                      int depth);

/* -----------------------------------------------------------------------
 * Named pattern registry
 *
 * Tracks which pattern variable names have been compiled to C functions
 * by byrd_emit_named_pattern().  E_INDR (*varname) checks here: if the
 * name is registered, emit a direct function call instead of going through
 * match_pattern_at().
 *
 * Registry entries:
 *   varname  — the SNOBOL4 variable name (e.g. "snoParse")
 *   typename — the C struct typedef name  (e.g. "pat_snoParse_t")
 *   fnname   — the C function name        (e.g. "pat_snoParse")
 * ----------------------------------------------------------------------- */

#define NAMED_PAT_MAX 128
#define NAME_LEN 320
#define NAMED_PAT_LBUF2 (NAME_LEN * 2 + 32)  /* compound: prefix+name+suffix+name */

typedef struct {
    char varname[NAME_LEN];
    char typename[NAME_LEN];
    char fnname[NAME_LEN];
    int  emitted;   /* 1 after byrd_emit_named_pattern has run for this name */
} NamedPat;

static NamedPat named_pats[NAMED_PAT_MAX];
static int      named_pat_count = 0;

void named_pat_reset(void) { named_pat_count = 0; }

/* -----------------------------------------------------------------------
 * Pending conditional assignments (E_CAPT_COND / dot-capture)
 *
 * emit_cond() registers (varname, c_tmpvar) pairs here.
 * emit.c calls cond_emit_assigns() at _byrd_ok to flush them.
 * cond_reset() is called before each MATCH_fn block.
 * ----------------------------------------------------------------------- */
#define COND_ASSIGN_MAX 32
typedef struct {
    char varname[NAME_LEN];  /* SNOBOL4 var, e.g. "OUTPUT" */
    char tmpvar[NAME_LEN];   /* C temp var holding captured string */
    int  has_cstatic;                 /* 1 if a C static exists for varname */
} CondAssign;
static CondAssign cond_assigns[COND_ASSIGN_MAX];
static int        cond_assign_count = 0;

static void cond_reset(void) { cond_assign_count = 0; }

/* Called by emit_cond to register a pending conditional */
static void cond_register(const char *varname, const char *tmpvar, int has_cs) {
    if (cond_assign_count >= COND_ASSIGN_MAX) return;
    CondAssign *ca = &cond_assigns[cond_assign_count++];
    snprintf(ca->varname, NAME_LEN, "%s", varname);
    snprintf(ca->tmpvar,  NAME_LEN, "%s", tmpvar);
    ca->has_cstatic = has_cs;
}

/* Called by emit.c at _byrd_ok to flush pending conditional assigns */
static void cond_emit_assigns(FILE *fp, int stmt_u) {
    (void)stmt_u;
    for (int i = 0; i < cond_assign_count; i++) {
        CondAssign *ca = &cond_assigns[i];
        if (strcasecmp(ca->varname, "OUTPUT") == 0) {
            fprintf(fp, "if (%s) NV_SET_fn(\"OUTPUT\", STRVAL(%s));\n",
                    ca->tmpvar, ca->tmpvar);
        } else {
            fprintf(fp, "if (%s) { NV_SET_fn(\"%s\", STRVAL(%s));",
                    ca->tmpvar, ca->varname, ca->tmpvar);
            if (ca->has_cstatic) {
                /* sync C static too — use cs logic inline */
                char cs[NAMED_PAT_LBUF2];
                snprintf(cs, sizeof cs, "_%s", ca->varname);
                fprintf(fp, " %s = STRVAL(%s);", cs, ca->tmpvar);
            }
            fprintf(fp, " }\n");
        }
    }
}

static void named_pat_register(const char *varname,
                                const char *typename,
                                const char *fnname) {
    /* Deduplicate */
    for (int i = 0; i < named_pat_count; i++)
        if (strcmp(named_pats[i].varname, varname) == 0) return;
    if (named_pat_count >= NAMED_PAT_MAX) return;
    NamedPat *e = &named_pats[named_pat_count++];
    snprintf(e->varname,  NAME_LEN, "%s", varname);
    snprintf(e->typename, NAME_LEN, "%s", typename);
    snprintf(e->fnname,   NAME_LEN, "%s", fnname);
}

/* Pre-register a name without emitting anything — so all names are known
 * before any function body is emitted (handles forward/mutual references). */
void scan_named_patterns(const char *varname) {
    char safe[NAMED_PAT_LBUF2];
    const char *s = varname;
    int i = 0;
    for (; *s && i < (int)(sizeof safe)-1; s++, i++)
        safe[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
    safe[i] = '\0';
    char fnname[NAME_LEN + 16], tyname[NAME_LEN + 16];
    snprintf(fnname, sizeof fnname, "pat_%s", safe);
    snprintf(tyname, sizeof tyname, "pat_%s_t", safe);
    named_pat_register(varname, tyname, fnname);
}

/* Emit typedef forward-declarations for all registered named pattern structs.
 * Must be called BEFORE byrd_emit_named_fwdecls so the struct pointer types
 * in function prototypes are valid incomplete-type pointers.
 *   typedef struct pat_X_t pat_X_t;   (incomplete — enough for pointer use)
 */
static void emit_named_typedecls(FILE *out_file) {
    for (int i = 0; i < named_pat_count; i++) {
        fprintf(out_file,
            "typedef struct %s %s;\n",
            named_pats[i].typename,
            named_pats[i].typename);
    }
    /* Bug5: one file-scope static for cross-pattern nInc frame threading.
     * pat_Expr4 (or any pattern with saved_frame) sets this before calling
     * a child named pattern that contains nInc().  The child reads it at
     * entry and stores it in its own _parent_frame field. */
    fprintf(out_file, "static int _pending_parent_frame = -1;\n");
    if (named_pat_count > 0) fprintf(out_file, "\n");
}

/* Emit forward declarations for ALL registered named patterns.
 * Must be called once, after all scan_named_patterns calls,
 * and BEFORE any byrd_emit_named_pattern calls.
 * Signature: pat_X(subj, slen, cur_ptr, zz, entry) */
static void emit_named_fwdecls(FILE *out_file) {
    for (int i = 0; i < named_pat_count; i++) {
        fprintf(out_file,
            "static DESCR_t %s(const char *, int64_t, int64_t *, %s **, int);\n",
            named_pats[i].fnname,
            named_pats[i].typename);
    }
    if (named_pat_count > 0) fprintf(out_file, "\n");
}

static const NamedPat *named_pat_lookup(const char *varname) {
    for (int i = 0; i < named_pat_count; i++)
        if (strcmp(named_pats[i].varname, varname) == 0)
            return &named_pats[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * Local storage declarations
 *
 * Two modes:
 *   Normal (in_named_pat == 0): emit `static TYPE name;` as before.
 *   Struct  (in_named_pat == 1): collect into a struct typedef, emit
 *     `#define name z->name` aliases so the code body is unchanged.
 *
 * Child frame pointers for E_INDR calls inside a named pattern are
 * collected in child_decl_buf (separate buffer, same lifetime).
 * ----------------------------------------------------------------------- */

#define DECL_BUF_MAX  256
#define DECL_LINE_MAX (NAME_LEN + 32)

static char decl_buf[DECL_BUF_MAX][DECL_LINE_MAX];
static int  decl_count;

/* Child frame pointer declarations: "struct pat_Y_t *Y_z_UID"
 * Collected during byrd_emit of a named pattern body. */
static char child_decl_buf[DECL_BUF_MAX][DECL_LINE_MAX];
static int  child_decl_count;

/* Cross-call dedup: tracks statics already emitted in the current C function.
 * Reset by fn_scope_reset() when emit.c opens a new C function. */
static char fn_seen[DECL_BUF_MAX][DECL_LINE_MAX];
static int  fn_seen_count;

/* Struct mode — set by byrd_emit_named_pattern */
static int  in_named_pat = 0;
/* Name of the pattern currently being emitted (valid when in_named_pat==1).
 * Used to gate output_str suppression: parse patterns suppress it; pp/Gen/ss/qq don't. */
static const char *current_named_pat_name = NULL;

/* Returns 1 if current named pattern is a parse-phase pattern (not a pp/output pattern).
 * Parse patterns build the Shift/Reduce tree — their $OUTPUT captures fire during parse
 * and must not emit immediately; Gen/pp handles actual output.
 * pp*, Gen, ss, qq, output* are pretty-print/output patterns that must NOT suppress. */
static int suppress_output_in_named_pat(void) {
    if (!in_named_pat || !current_named_pat_name) return 0;
    const char *n = current_named_pat_name;
    /* Never suppress in: pp*, Gen, ss, qq, output* */
    if (strncasecmp(n, "pp",     2) == 0) return 0;
    if (strncasecmp(n, "Gen",    3) == 0) return 0;
    if (strcasecmp (n, "ss")       == 0) return 0;
    if (strcasecmp (n, "qq")       == 0) return 0;
    if (strncasecmp(n, "output", 6) == 0) return 0;
    return 1;  /* parse pattern — suppress */
}

/* Bug5: when emit_seq sees left=nPush() and in_named_pat, it stores the uid
 * here so the immediately following E_OPSYN & can reference z->_saved_frame_UID
 * instead of ntop().  Reset to -1 after E_OPSYN consumes it. */
static int  pending_npush_uid = -1;

static void fn_scope_reset(void) { fn_seen_count = 0; }

static void decl_reset(void) {
    decl_count = 0;
    child_decl_count = 0;
}

static void decl_add(const char *fmt, ...) {
    if (decl_count >= DECL_BUF_MAX) return;
    char tmp[DECL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, DECL_LINE_MAX, fmt, ap);
    va_end(ap);
    /* Dedup within this pattern's decl buffer */
    for (int i = 0; i < decl_count; i++)
        if (strcmp(decl_buf[i], tmp) == 0) return;
    /* In non-struct mode: dedup across patterns in same C function */
    if (!in_named_pat) {
        for (int i = 0; i < fn_seen_count; i++)
            if (strcmp(fn_seen[i], tmp) == 0) return;
    }
    memcpy(decl_buf[decl_count++], tmp, DECL_LINE_MAX);
}

/* Add a child frame pointer field to child_decl_buf.
 * fmt must be a C declaration like "struct pat_Y_t *Y_z_7".
 * Returns the field name (last token) so the caller can reference z->fieldname. */
static void child_decl_add(const char *decl) {
    if (child_decl_count >= DECL_BUF_MAX) return;
    for (int i = 0; i < child_decl_count; i++)
        if (strcmp(child_decl_buf[i], decl) == 0) return;
    strncpy(child_decl_buf[child_decl_count++], decl, DECL_LINE_MAX - 1);
}

/* Extract the field name from a declaration string "TYPE name" or "TYPE *name"
 * or "TYPE name[N]". Returns a pointer into the string at the start of the name. */
/* Walk backwards over one character that is part of a C identifier,
 * including ASCII alnum/underscore AND UTF-8 multibyte sequences
 * (for Greek port suffixes α β γ ω which are 2-byte UTF-8). */
static int is_ident_byte(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 0x80;
}

static const char *decl_field_name(const char *decl) {
    /* If there's an array bracket, the name ends just before '[' */
    const char *bracket = strchr(decl, '[');
    const char *end = bracket ? bracket : decl + strlen(decl);
    /* Walk backwards over the identifier (ASCII + UTF-8 multibyte) */
    while (end > decl && is_ident_byte((unsigned char)end[-1])) end--;
    return end;
}

/* Length of the field name (needed when there's an array suffix). */
static int decl_field_name_len(const char *decl) {
    const char *bracket = strchr(decl, '[');
    const char *end = bracket ? bracket : decl + strlen(decl);
    const char *start = end;
    while (start > decl && is_ident_byte((unsigned char)start[-1])) start--;
    return (int)(end - start);
}

/* Emit normal local storage (non-struct mode).
 * These are plain (non-static) locals — declared before the first goto
 * (two-pass approach guarantees this) so C99 jump-over-declaration is not
 * an issue.  Must NOT be static: static locals persist across trampoline
 * calls and corrupt pattern frame state on the second+ statement. */
static void decl_flush(void) {
    if (decl_count == 0) return;
    B("    /* === local storage === */\n");
    for (int i = 0; i < decl_count; i++) {
        B("%s;\n", decl_buf[i]);
        /* Record in fn_seen so subsequent patterns skip this decl */
        if (fn_seen_count < DECL_BUF_MAX)
            memcpy(fn_seen[fn_seen_count++], decl_buf[i], DECL_LINE_MAX);
    }
    B("\n");
}

/* Emit struct typedef + field #defines for named pattern struct mode.
 * tyname = "pat_X_t", safe = "X"
 * Writes to out_file (not out — called before code body is spliced). */
static void decl_flush_as_struct(FILE *out_file, const char *tyname) {
    /* Struct typedef */
    fprintf(out_file, "typedef struct %s {\n", tyname);
    for (int i = 0; i < decl_count; i++)
        fprintf(out_file, "    %s;\n", decl_buf[i]);
    for (int i = 0; i < child_decl_count; i++)
        fprintf(out_file, "    %s;\n", child_decl_buf[i]);
    fprintf(out_file, "} %s;\n\n", tyname);
}

/* Emit #define aliases so the code body can use bare names via z->name. */
static void decl_emit_defines(FILE *out_file) {
    for (int i = 0; i < decl_count; i++) {
        const char *nm = decl_field_name(decl_buf[i]);
        int len = decl_field_name_len(decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#define %.*s z->%.*s\n", len, nm, len, nm);
    }
    for (int i = 0; i < child_decl_count; i++) {
        const char *nm = decl_field_name(child_decl_buf[i]);
        int len = decl_field_name_len(child_decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#define %.*s z->%.*s\n", len, nm, len, nm);
    }
    if (decl_count + child_decl_count > 0)
        fprintf(out_file, "\n");
}

/* Emit #undef for all defines. */
static void decl_emit_undefs(FILE *out_file) {
    for (int i = 0; i < decl_count; i++) {
        const char *nm = decl_field_name(decl_buf[i]);
        int len = decl_field_name_len(decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#undef %.*s\n", len, nm);
    }
    for (int i = 0; i < child_decl_count; i++) {
        const char *nm = decl_field_name(child_decl_buf[i]);
        int len = decl_field_name_len(child_decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#undef %.*s\n", len, nm);
    }
    if (decl_count + child_decl_count > 0)
        fprintf(out_file, "\n");
}

/* -----------------------------------------------------------------------
 * C-safe string literal emission
 * ----------------------------------------------------------------------- */

/* escape_string defined above */

/* -----------------------------------------------------------------------
 * emit_charset_cexpr — build a C expression (written into buf) that evaluates
 * at runtime to a `const char *` charset string, given an EXPR_t* AST node.
 *
 * E_QLIT  → "literal"          (compile-time constant)
 * E_VAR  → VARVAL_fn(NV_GET_fn("name"))
 * E_CONCAT → CONCAT_fn(lhs, rhs)   (both sides recursed)
 * fallback → ""
 * ----------------------------------------------------------------------- */
static void emit_charset_cexpr(EXPR_t *arg, char *buf, int bufsz);

static void emit_charset_cexpr(EXPR_t *arg, char *buf, int bufsz) {
    if (!arg) { snprintf(buf, bufsz, "\"\""); return; }
    switch (arg->kind) {
    case E_QLIT: {
        /* Build a quoted C literal via escape_string logic, inline into buf */
        const char *s = arg->sval ? arg->sval : "";
        int pos = 0;
        buf[pos++] = '"';
        for (; *s && pos < bufsz - 4; s++) {
            if (*s == '"' || *s == '\\') buf[pos++] = '\\';
            else if (*s == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; continue; }
            else if (*s == '\t') { buf[pos++] = '\\'; buf[pos++] = 't'; continue; }
            buf[pos++] = *s;
        }
        buf[pos++] = '"';
        buf[pos]   = '\0';
        return;
    }
    case E_VAR:
        snprintf(buf, bufsz, "VARVAL_fn(NV_GET_fn(\"%s\"))",
                 arg->sval ? arg->sval : "");
        return;
    case E_CONCAT: {
        char lbuf[512], rbuf[512];
        emit_charset_cexpr(arg->children[0],  lbuf, (int)sizeof lbuf);
        emit_charset_cexpr(arg->children[1], rbuf, (int)sizeof rbuf);
        /* CONCAT_fn is #define'd as CONCAT_fn(DESCR_t,DESCR_t) in snoc_runtime.h,
         * so we must wrap char* sides in STRVAL and extract result with VARVAL_fn. */
        snprintf(buf, bufsz,
                 "VARVAL_fn(CONCAT_fn(STRVAL(%s), STRVAL(%s)))",
                 lbuf, rbuf);
        return;
    }
    case E_KW:
        /* &UCASE, &LCASE etc — kw(name) = NV_GET_fn(name) → DESCR_t */
        snprintf(buf, bufsz, "VARVAL_fn(NV_GET_fn(\"%s\"))",
                 arg->sval ? arg->sval : "");
        return;
    case E_INDR:
        /* $varname — indirect: left child holds the var name node */
        if (arg->children[0] && arg->children[0]->sval)
            snprintf(buf, bufsz, "VARVAL_fn(NV_GET_fn(\"%s\"))", arg->children[0]->sval);
        else
            snprintf(buf, bufsz, "\"\"");
        return;
    default:
        fprintf(stderr, "emit_charset_cexpr: unhandled kind=%d sval=%s\n",
                (int)arg->kind, arg->sval ? arg->sval : "(null)");
        snprintf(buf, bufsz, "\"\"");
        return;
    }
}

/* Forward declarations for leaf emitters */
static void emit_lit(const char *s,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor);
static void emit_pos(long n, const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *cursor);
static void emit_pos_expr(const char *expr, const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor);
static void emit_rpos(long n, const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj_len, const char *cursor);
static void emit_rpos_expr(const char *expr, const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *subj_len, const char *cursor);
static void emit_len(long n, const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj_len, const char *cursor);
static void emit_tab(long n, const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *cursor);
static void emit_tab_expr(const char *expr, const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor);
static void emit_rtab(long n, const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj_len, const char *cursor);
static void emit_rtab_expr(const char *expr, const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *subj_len, const char *cursor);
static void emit_any(const char *cs,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor);
static void emit_notany(const char *cs,
                        const char *α, const char *β,
                        const char *γ, const char *ω,
                        const char *subj, const char *subj_len,
                        const char *cursor);
static void emit_span(const char *cs,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj, const char *subj_len,
                      const char *cursor);
static void emit_break(const char *cs,
                       const char *α, const char *β,
                       const char *γ, const char *ω,
                       const char *subj, const char *subj_len,
                       const char *cursor);
static void emit_breakx(const char *cs,
                        const char *α, const char *β,
                        const char *γ, const char *ω,
                        const char *subj, const char *subj_len,
                        const char *cursor);
static void emit_arb(const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj_len, const char *cursor);
static void emit_rem(const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj_len, const char *cursor);
static void emit_fence(const char *α, const char *β,
                       const char *γ, const char *ω);
static void emit_succeed(const char *α, const char *β,
                         const char *γ);
static void emit_fail(const char *α, const char *β,
                           const char *ω);
static void emit_abort(const char *α, const char *β,
                            const char *ω);


static void emit_seq(EXPR_t *left, EXPR_t *right,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth);
static void emit_alt(EXPR_t *left, EXPR_t *right,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth);
static void emit_arbno(EXPR_t *child,
                       const char *α, const char *β,
                       const char *γ, const char *ω,
                       const char *subj, const char *subj_len,
                       const char *cursor, int depth);
static void emit_imm(EXPR_t *child, const char *varname,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth, int do_shift);


/* -----------------------------------------------------------------------
 * LIT node
 *
 * Oracle (sprint1/lit_hello.c):
 *   lit1_α:
 *       if (cursor + 5 > subject_len) goto lit1_ω;
 *       if (memcmp(subject + cursor, "hello", 5) != 0) goto lit1_ω;
 *       lit1_saved_cursor = cursor;
 *       cursor += 5;
 *       goto lit1_γ;
 *   lit1_β:
 *       cursor = lit1_saved_cursor;
 *       goto lit1_ω;
 * ----------------------------------------------------------------------- */

static void emit_lit(const char *s,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor) {
    int n = (int)strlen(s);
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    /* α: bounds check, match, advance */
    PLG(α, NULL);
    PS(ω,   "if (%s + %d > %s)", cursor, n, subj_len);
    if (n == 1) {
        char ch = s[0];
        if (ch == '\'' || ch == '\\')
            PS(ω, "if (%s[%s] != '\\%c')", subj, cursor, ch);
        else
            PS(ω, "if (%s[%s] != '%c')", subj, cursor, ch);
    } else {
        /* memcmp: build stmt string with embedded string literal */
        char cstr[LBUF*2]; int ci = 0;
        cstr[ci++] = '"';
        for (int i = 0; i < n && ci < (int)sizeof(cstr)-4; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == '"' || c == '\\') { cstr[ci++] = '\\'; cstr[ci++] = c; }
            else if (c < 32)           { ci += snprintf(cstr+ci, 8, "\\x%02x", c); }
            else                       { cstr[ci++] = c; }
        }
        cstr[ci++] = '"'; cstr[ci] = '\0';
        PS(ω, "if (memcmp(%s + %s, %s, %d) != 0)", subj, cursor, cstr, n);
    }
    PS(γ, "%s = %s; %s += %d;", saved, cursor, cursor, n);

    /* β: restore cursor */
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * POS node
 *
 * Oracle (sprint2/cat_pos_lit_rpos.c):
 *   pos1_α:
 *       if (cursor != 0) goto pos1_ω;
 *       goto pos1_γ;
 *   pos1_β:
 *   pos1_ω:
 *       goto match_fail;
 * ----------------------------------------------------------------------- */

static void emit_pos(long n,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *cursor) {
    PLG(α, NULL);
    PS(ω,  "if (%s != %ld)", cursor, n);
    PG(γ);
    PLG(β, ω);
}

/* POS with runtime expression (variable arg) */
static void emit_pos_expr(const char *expr,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor) {
    PLG(α, NULL);
    PS(ω,  "if (%s != (int64_t)(%s))", cursor, expr);
    PG(γ);
    PLG(β, ω);
}

/* -----------------------------------------------------------------------
 * RPOS node
 *
 * Oracle:
 *   rpos1_α:
 *       if (cursor != subject_len - 0) goto rpos1_ω;
 *       goto rpos1_γ;
 *   rpos1_β:
 *       goto rpos1_ω;
 * ----------------------------------------------------------------------- */

static void emit_rpos(long n,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj_len, const char *cursor) {
    PLG(α, NULL);
    PS(ω,  "if (%s != %s - %ld)", cursor, subj_len, n);
    PG(γ);
    PLG(β, ω);
}

/* RPOS with runtime expression */
static void emit_rpos_expr(const char *expr,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *subj_len, const char *cursor) {
    PLG(α, NULL);
    PS(ω,  "if (%s != %s - (int64_t)(%s))", cursor, subj_len, expr);
    PG(γ);
    PLG(β, ω);
}

/* -----------------------------------------------------------------------
 * LEN node
 *
 * Match exactly n characters unconditionally.
 * ----------------------------------------------------------------------- */

static void emit_len(long n,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s + %ld > %s)", cursor, n, subj_len);
    PS(γ,  "%s = %s; %s += %ld;", saved, cursor, cursor, n);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * TAB node — advance cursor to absolute position n
 * ----------------------------------------------------------------------- */

static void emit_tab(long n,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s > %ld)", cursor, n);
    PS(γ,  "%s = %s; %s = %ld;", saved, cursor, cursor, n);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* TAB with runtime expression */
static void emit_tab_expr(const char *expr,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s > (int64_t)(%s))", cursor, expr);
    PS(γ,  "%s = %s; %s = (int64_t)(%s);", saved, cursor, cursor, expr);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * RTAB node — advance cursor to len - n
 * ----------------------------------------------------------------------- */

static void emit_rtab(long n,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s > %s - %ld)", cursor, subj_len, n);
    PS(γ,  "%s = %s; %s = %s - %ld;", saved, cursor, cursor, subj_len, n);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* RTAB with runtime expression */
static void emit_rtab_expr(const char *expr,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s > %s - (int64_t)(%s))", cursor, subj_len, expr);
    PS(γ,  "%s = %s; %s = %s - (int64_t)(%s);", saved, cursor, cursor, subj_len, expr);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * ANY node — match one char in charset
 * ----------------------------------------------------------------------- */

static void emit_any(const char *cs,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s >= %s)", cursor, subj_len);
    PS(ω,  "if (!strchr(%s, %s[%s]))", cs, subj, cursor);
    PS(γ,  "%s = %s; %s++;", saved, cursor, cursor);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * NOTANY node — match one char NOT in charset
 * ----------------------------------------------------------------------- */

static void emit_notany(const char *cs,
                        const char *α, const char *β,
                        const char *γ, const char *ω,
                        const char *subj, const char *subj_len,
                        const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(ω,  "if (%s >= %s)", cursor, subj_len);
    PS(ω,  "if (strchr(%s, %s[%s]))", cs, subj, cursor);
    PS(γ,  "%s = %s; %s++;", saved, cursor, cursor);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * SPAN node — MATCH_fn one or more chars in charset
 *
 * On β: retreat one char at a time (backtrackable).
 * ----------------------------------------------------------------------- */

static void emit_span(const char *cs,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj, const char *subj_len,
                      const char *cursor) {
    char delta[LBUF], start[LBUF];
    snprintf(delta, LBUF, "%s_delta", α);
    snprintf(start, LBUF, "%s_start", α);
    decl_add("int64_t %s", delta);
    decl_add("int64_t %s", start);

    PLG(α, NULL);
    PS(NULL,   "%s = %s;", start, cursor);
    PS(NULL,   "while (%s < %s && strchr(%s, %s[%s])) %s++;",
               cursor, subj_len, cs, subj, cursor, cursor);
    PS(NULL,   "%s = %s - %s;", delta, cursor, start);
    PS(ω,  "if (%s == 0)", delta);
    PG(γ);

    PLG(β, NULL);
    B("                  if (%s <= 1) { %s = %s; goto %s; }\n",
      delta, cursor, start, ω);
    PS(γ,  "%s--; %s--;", delta, cursor);
}

/* -----------------------------------------------------------------------
 * BREAK node — match zero or more chars NOT in charset, stop before hit
 * Deterministic: β → ω.
 * ----------------------------------------------------------------------- */

static void emit_break(const char *cs,
                       const char *α, const char *β,
                       const char *γ, const char *ω,
                       const char *subj, const char *subj_len,
                       const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PLG(α, NULL);
    PS(NULL,  "%s = %s;", saved, cursor);
    PS(NULL,  "while (%s < %s && !strchr(%s, %s[%s])) %s++;",
              cursor, subj_len, cs, subj, cursor, cursor);
    /* Fail-check: if we hit end-of-subject without finding delimiter, restore+fail.
     * Use explicit goto inside braces so wrap never splits the condition from its goto. */
    B("                  if (%s >= %s) { %s = %s; goto %s; }\n",
      cursor, subj_len, cursor, saved, ω);
    PG(γ);
    PL(β, ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * BREAKX node — like BREAK but β advances 1 past the break-char and retries.
 *
 * SPITBOL semantics (from sbl.asm p_bkx / s_bkx):
 *   BREAKX(cs)  ≡  BREAK(cs) ARBNO(LEN(1) BREAK(cs))
 *
 * Byrd box wiring:
 *   α: scan forward while not in cs.
 *      if end-of-subject → ω (cs-char never found, fail).
 *      else → γ (stopped AT a cs-char, succeed).
 *   β: advance cursor 1 past the current cs-char, then scan again.
 *      if end-of-subject after advance → ω.
 *      else → γ.
 *
 * Key difference from BREAK: β does NOT restore cursor — it extends.
 * ----------------------------------------------------------------------- */
static void emit_breakx(const char *cs,
                        const char *α, const char *β,
                        const char *γ, const char *ω,
                        const char *subj, const char *subj_len,
                        const char *cursor) {
    /* shared scan label: both α and β funnel here */
    char scan_lbl[LBUF];
    snprintf(scan_lbl, LBUF, "%s_bkx_scan", α);

    PLG(α, scan_lbl);
    PLG(β,  NULL);
    /* β: advance 1 past the break-char, then fall into scan */
    B("                  if (%s >= %s) goto %s;\n", cursor, subj_len, ω);
    B("                  %s++;\n", cursor);          /* skip the cs-char */
    B("    %s:\n", scan_lbl);
    /* scan loop: advance while NOT in cs */
    B("                  while (%s < %s && !strchr(%s, %s[%s])) %s++;\n",
      cursor, subj_len, cs, subj, cursor, cursor);
    B("                  if (%s >= %s)                           goto %s;\n",
      cursor, subj_len, ω);
    PG(γ);
}

static void emit_arb(const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PL(α, γ, "%s = %s;", saved, cursor);

    PLG(β, NULL);
    PS(ω, "if (kw_anchor || %s >= %s)", saved, subj_len);
    PS(γ, "%s++; %s = %s;", saved, cursor, saved);
}

/* -----------------------------------------------------------------------
 * REM node — match the rest of the subject
 * ----------------------------------------------------------------------- */

static void emit_rem(const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", α);
    decl_add("int64_t %s", saved);

    PL(α, γ, "%s = %s; %s = %s;", saved, cursor, cursor, subj_len);
    PL(β,  ω, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * SEQ (CAT) node
 *
 * Wiring (from lower.py _emit Seq):
 *   α     → left_α
 *   β     → right_β   (first try resuming right; if exhausted → left_β)
 *   left succeeds  → right_α
 *   left fails     → ω
 *   right succeeds → γ
 *   right fails    → left_β  (backtrack left)
 *
 * Oracle (sprint2):
 *   root_α:  goto cat_l1_α;
 *   cat_l1_*     POS
 *   cat_l1_γ: goto cat_r2_α;
 *   cat_l1_ω: goto root_β;
 *   cat_r2_*     ...
 *   cat_r2_ω: goto cat_l1_β;
 * ----------------------------------------------------------------------- */

static void emit_seq(EXPR_t *left, EXPR_t *right,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth) {
    int uid = next_uid();

    Label left_α, left_β, right_α, right_β;
    label_fmt(left_α,  "cat_l", uid, "α");
    label_fmt(left_β,  "cat_l", uid, "β");
    label_fmt(right_α, "cat_r", uid, "α");
    label_fmt(right_β, "cat_r", uid, "β");

    PLG(α, left_α);   /* α  → left_α  (CAT — ENTER_fn left) */
    /* β → right_β (resume right first).
     * Special case: if left is nPush(), the backtrack path enters right_β
     * without ever having passed through left_α (where NPUSH_fn fires on
     * the forward path).  Emit NPUSH_fn() here so ntop() is valid when
     * the right child's Reduce/E_OPSYN fires on the backtrack path.
     *
     * Bug5 fix: also save the frame index so E_OPSYN Reduce can read
     * NSTACK_AT_fn(_saved_frame) instead of ntop() — which would be
     * wrong when nested NPUSHes have displaced _ntop above this frame.
     * In struct mode (in_named_pat), add a _saved_frame_%d field to the
     * struct so the saved index survives across entry/re-entry. */
    if (left && left->kind == E_FNC && left->sval &&
        strcasecmp(left->sval, "nPush") == 0) {
        if (suppress_output_in_named_pat()) {
            char sf_field[64];
            snprintf(sf_field, sizeof sf_field, "int _saved_frame_%d", uid);
            decl_add("%s", sf_field);
            pending_npush_uid = uid;  /* tell E_FNC nPush emitter to save frame */
            /* Fall through to normal PLG/byrd_emit — E_FNC nPush will pick up
             * pending_npush_uid and emit the NTOP_INDEX_fn() save inline. */
            PLG(β, right_β);
        } else {
            PL(β, right_β, "NPUSH_fn();");  /* β: re-push before retrying right */
        }
    } else {
        PLG(β, right_β);
    }

    emit_pat_node(left,
              left_α, left_β,
              right_α, ω,
              subj, subj_len, cursor, depth + 1);

    emit_pat_node(right,
              right_α, right_β,
              γ, left_β,
              subj, subj_len, cursor, depth + 1);

    /* Bug5: if THIS emit_seq set pending_npush_uid (left was nPush()) and
     * E_OPSYN never consumed it (right didn't contain E_OPSYN), reset it now.
     * We only reset if we were the setter — tracked by whether uid matches. */
    if (in_named_pat && left && left->kind == E_FNC && left->sval &&
        strcasecmp(left->sval, "nPush") == 0) {
        pending_npush_uid = -1;  /* nPush seq done — clear any unconsumed uid */
    }
}

/* -----------------------------------------------------------------------
 * ALT node
 *
 * Wiring (from lower.py _emit Alt):
 *   α     → left_α
 *   β     → IndirectGoto(t)  — resume active arm's β
 *   left_γ  → save t=left_β; goto γ
 *   left_ω  → right_α
 *   right_γ → save t=right_β; goto γ
 *   right_ω → ω
 *
 * For C (emit_c_byrd.py): t is an int local; switch on t.
 *
 * Oracle (sprint3/alt_first.c):
 *   cat_l3_α:  [ALT — try left]
 *       goto alt_l5_α;
 *   alt_l5_α:   [left arm]   -> cat_r4_α (γ)
 *   alt_l5_β:    [backtrack]  -> alt_r6_α (right)
 *   alt_r6_α:   [right arm]  -> cat_r4_α (γ)
 *   alt_r6_β:    [backtrack]  -> cat_r2_β  (ω)
 *   cat_l3_β:    goto alt_r6_β;
 * ----------------------------------------------------------------------- */

static void emit_alt(EXPR_t *left, EXPR_t *right,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth) {
    int uid = next_uid();

    Label left_α, left_β, right_α, right_β;
    label_fmt(left_α,  "alt_l", uid, "α");
    label_fmt(left_β,  "alt_l", uid, "β");
    label_fmt(right_α, "alt_r", uid, "α");
    label_fmt(right_β, "alt_r", uid, "β");

    PLG(α, left_α);   /* α → left_α  (ALT — try left) */
    PLG(β,  right_β);  /* β → right_β (backtrack right arm) */

    emit_pat_node(left,
              left_α, left_β,
              γ, right_α,
              subj, subj_len, cursor, depth + 1);

    emit_pat_node(right,
              right_α, right_β,
              γ, ω,
              subj, subj_len, cursor, depth + 1);
}

/* -----------------------------------------------------------------------
 * ARBNO node
 *
 * Wiring (from lower.py _emit Arbno and oracle sprint5/arbno_match.c):
 *
 *   cat_l3_α:
 *       cat_l3_depth = -1;
 *       goto cat_r4_α;           // ARBNO: zero matches → succeed
 *   cat_l3_β:                     // ARBNO resume: extend by one
 *       cat_l3_depth++;
 *       if (cat_l3_depth >= 64) goto cat_r2_β;
 *       cat_l3_cursors[cat_l3_depth] = cursor;
 *       goto arbno_c5_α;
 *   arbno_c5_α: [child α]
 *   arbno_c5_β:  [child β]     → cat_l3_child_fail
 *   cat_l3_child_ok:  goto cat_r4_α;   // child matched → ARBNO ok
 *   cat_l3_child_fail:
 *       cursor = cat_l3_cursors[cat_l3_depth];
 *       cat_l3_depth--;
 *       goto cat_r2_β;            // child failed → ARBNO fails
 *
 * The key insight from the oracle:
 *   - ARBNO starts by succeeding with ZERO matches (depth=-1, goto γ)
 *   - β extends: save cursor, try child
 *   - child_ok → γ (ARBNO succeeds again with one more MATCH_fn)
 *   - child_fail → restore cursor, depth--, ω (ARBNO gives up)
 * ----------------------------------------------------------------------- */

static void emit_arbno(EXPR_t *child,
                       const char *α, const char *β,
                       const char *γ, const char *ω,
                       const char *subj, const char *subj_len,
                       const char *cursor, int depth) {
    int uid = next_uid();

    Label child_α, child_β, child_ok, child_fail;
    label_fmt(child_α, "arbno_c", uid, "α");
    label_fmt(child_β, "arbno_c", uid, "β");
    snprintf(child_ok,   LBUF, "%s_child_ok",   α);
    snprintf(child_fail, LBUF, "%s_child_fail", α);

    char depth_var[LBUF], stack_var[LBUF], stk_save[LBUF];
    snprintf(depth_var, LBUF, "%s_depth",   α);
    snprintf(stack_var, LBUF, "%s_cursors", α);
    snprintf(stk_save,  LBUF, "%s_saved_stk", α);
    decl_add("int %s",         depth_var);
    decl_add("int64_t %s[64]", stack_var);
    /* Save/restore $'@S' (tree stack) across ARBNO iterations.
     * The zero-match α path fires Reduce immediately, pushing a tree
     * onto @S.  When ARBNO extends via β that stray tree must be undone
     * before the next Reduce fires, or Reduce(STMT_t,7) consumes it as a child. */
    decl_add("DESCR_t %s",      stk_save);

    /* α: snapshot @S, zero matches → succeed immediately */
    PL(α, γ, "%s = NV_GET_fn(\"@S\"); %s = -1;", stk_save, depth_var);

    /* β: restore @S to pre-α snapshot, then extend by one */
    PLG(β, NULL);
    /* Undo any Reduce side-effects from the previous shorter match */
    PS(NULL, "NV_SET_fn(\"@S\", %s);", stk_save);
    /* Re-establish counter frame if nPop fired on the α-path success */
    PS(NULL, "if (!NHAS_FRAME_fn()) NPUSH_fn();");
    PS(ω,    "if (++%s >= 64)", depth_var);
    PS(child_α,  "%s[%s] = %s;", stack_var, depth_var, cursor);

    /* child_ok: child matched → update @S snapshot, ARBNO offers another match */
    PLG(child_ok, NULL);
    /* Snapshot @S after successful child so next β restores to this point */
    PS(γ, "%s = NV_GET_fn(\"@S\");", stk_save);

    /* child_fail: child failed → restore cursor, POP_fn, ARBNO fails */
    PLG(child_fail, NULL);
    PS(ω,   "%s = %s[%s]; %s--;", cursor, stack_var, depth_var, depth_var);

    emit_pat_node(child,
              child_α, child_β,
              child_ok, child_fail,
              subj, subj_len, cursor, depth + 1);
}

/* -----------------------------------------------------------------------
 * is_sideeffect_call — returns 1 if e is an E_FNC to a counter side-effect
 * (nPush/nInc/nPop/nDec/nTop) that has no pattern-matching semantics.
 * ----------------------------------------------------------------------- */

static int is_sideeffect_call(EXPR_t *e) {
    if (!e || e->kind != E_FNC || !e->sval) return 0;
    return (strcasecmp(e->sval, "nPush") == 0 ||
            strcasecmp(e->sval, "nInc")  == 0 ||
            strcasecmp(e->sval, "nPop")  == 0 ||
            strcasecmp(e->sval, "nDec")  == 0 ||
            strcasecmp(e->sval, "nTop")  == 0);
}

/* emit_sideeffect_call_inline — emit a single C statement for a side-effect call */
static void emit_sideeffect_call_inline(EXPR_t *e) {
    if (!e || e->kind != E_FNC || !e->sval) return;
    if (strcasecmp(e->sval, "nPush") == 0) B("NPUSH_fn();");
    else if (strcasecmp(e->sval, "nInc") == 0) B("NINC_fn();");
    else if (strcasecmp(e->sval, "nPop") == 0) B("NPOP_fn();");
    else if (strcasecmp(e->sval, "nDec") == 0) B("NDEC_fn();");
    else if (strcasecmp(e->sval, "nTop") == 0) B("(void)ntop();");
    else B("/* unknown sideeffect %s */", e->sval);
}

/* -----------------------------------------------------------------------
 * E_CAPT_IMM ($ capture) node
 *
 * Pattern node:  <child> $ var
 * On child success: record span [start..cursor] into var, goto γ.
 * On β: undo the assignment (restore cursor is sufficient — var may linger).
 *
 * Oracle (sprint4/assign_lit.c):
 *   cat_l5_α:
 *       cat_l5_start = cursor;
 *       goto assign_c7_α;
 *   [child]  →  cat_l5_do_assign
 *   cat_l5_do_assign:
 *       var_OUTPUT.ptr = subject + cat_l5_start;
 *       var_OUTPUT.len = cursor - cat_l5_start;
 *       output(var_OUTPUT);
 *       goto cat_r6_α;
 *   cat_l5_β:
 *       goto assign_c7_β;
 *
 * Special case — Gimpel SNOBOL4 side-effect pattern: nPush() $'('
 *   The grammar parses this as E_CAPT_IMM(left=nPush(), right=E_QLIT("(")).
 *   left is NOT a pattern child — it's a side-effect call with no cursor advance.
 *   right is NOT a variable name — it's a literal to match and capture to OUTPUT.
 *   Fix: emit the side-effect call inline, then match+capture the literal.
 * ----------------------------------------------------------------------- */

static void emit_imm(EXPR_t *child, const char *varname,
                     const char *α, const char *β,
                     const char *γ, const char *ω,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth, int do_shift) {
    int uid = next_uid();

    /* -------------------------------------------------------------------
     * Special case: nPush() $'(' — side-effect call + literal capture.
     *
     * Grammar parses `nPush() $'('` as E_CAPT_IMM(left=nPush(), right=E_QLIT("(")).
     * child (left) = nPush() — no cursor advance, just a side-effect.
     * varname (from right) = "(" — not a valid variable; it IS the literal.
     *
     * Correct semantics: emit the side-effect inline, then match+capture
     * the literal string (varname) to OUTPUT.  β → ω (no backtrack over
     * a side-effect call — the call already fired).
     * ------------------------------------------------------------------- */
    if (is_sideeffect_call(child) && varname && varname[0] != '\0') {
        /* Check that varname looks like a non-identifier (e.g. punctuation
         * like '(' or ')') — if it IS a valid identifier, it could be a
         * genuine capture variable paired with a side-effect, which we
         * handle the same way (side-effect fires, then match literal). */
        int uid2 = next_uid();
        Label lit_α, lit_β;
        label_fmt(lit_α, "imm_se_lit", uid2, "α");
        label_fmt(lit_β, "imm_se_lit", uid2, "β");

        char start_var[LBUF], do_assign[LBUF];
        snprintf(start_var, LBUF, "%s_start",     α);
        snprintf(do_assign, LBUF, "%s_do_assign", α);
        decl_add("int64_t %s", start_var);

        /* α: emit side-effect inline, record start, enter literal match */
        PLG(α, NULL);
        PS(NULL, "%s_start = %s;", α, cursor);
        emit_sideeffect_call_inline(child);
        B("\n");
        PS(lit_α, "goto %s;", lit_α);

        /* literal match for varname */
        emit_lit(varname,
                 lit_α, lit_β,
                 do_assign, ω,
                 subj, subj_len, cursor);

        /* do_assign: capture matched span → OUTPUT, then γ */
        PLG(do_assign, NULL);
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = (char*)GC_malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        if (suppress_output_in_named_pat()) {
            PS(NULL,  "  (void)_os; /* output_str suppressed in named pat */");
            PS(γ, "  free(_os); }");
        } else {
            PS(γ, "  output_str(_os); free(_os); }");
        }

        /* β → ω: side-effect already fired, cannot resume */
        PLG(β, ω);
        return;
    }

    /* -------------------------------------------------------------------
     * Normal E_CAPT_IMM path: child is a real pattern, varname is a variable.
     * ------------------------------------------------------------------- */

    /* Detect literal-token varname: $'(' where the RHS is a punctuation
     * literal (all chars non-alnum/non-underscore), e.g. '(', ')', ','.
     * In beauty.sno this appears as `child $'('` meaning: child matches,
     * THEN the next token in the subject must be the literal string varname.
     * This is distinct from a normal capture variable like `child $MyVar`. */
    int is_literal_tok = 0;
    if (!do_shift && varname && varname[0] != '\0') {
        is_literal_tok = 1;
        for (const char *p = varname; *p; p++) {
            if (isalnum((unsigned char)*p) || *p == '_') { is_literal_tok = 0; break; }
        }
    }

    /* Sanitize varname: non-alnum/underscore chars → '_'.
     * Skip sanitization for do_shift=1 (~ operator): the tag IS the literal
     * text (e.g. "=", "Label", ":()") and must reach Shift() verbatim.
     * Only sanitize for C-identifier use (label/static names), not for the
     * STRVAL tag passed to Shift(). */
    char safe_varname[NAMED_PAT_LBUF2];
    if (!do_shift && !is_literal_tok) {
        int i = 0; const char *s = varname;
        for (; *s && i < (int)(sizeof safe_varname)-1; s++, i++)
            safe_varname[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
        safe_varname[i] = '\0';
        varname = safe_varname;
    }

    Label child_α, child_β;
    label_fmt(child_α, "assign_c", uid, "α");
    label_fmt(child_β, "assign_c", uid, "β");

    char start_var[LBUF], do_assign[LBUF];
    snprintf(start_var, LBUF, "%s_start",     α);
    snprintf(do_assign, LBUF, "%s_do_assign", α);
    decl_add("int64_t %s", start_var);

    /* α: record start, enter child — emitted inside is_literal_tok branches below */

    /* When varname is a literal token (e.g. '('), the child matches first,
     * then we must match the literal token before proceeding to γ.
     * Route child's γ to a literal-check label, not directly to do_assign. */
    if (is_literal_tok) {
        Label lit_check, lit_check_β;
        label_fmt(lit_check,   "dlit", next_uid(), "α");
        label_fmt(lit_check_β, "dlit", uid,        "β");
        /* saved cursor for the literal match backtrack */
        char lit_saved[LBUF];
        snprintf(lit_saved, LBUF, "%s_α_saved_cursor", lit_check);
        decl_add("int64_t %s", lit_saved);
        /* save @S before child runs so we can undo Shift() on literal fail */
        char stk_depth[LBUF];
        snprintf(stk_depth, LBUF, "dlit_%d_stk_depth", uid);
        decl_add("int %s", stk_depth);

        /* α: snapshot stack depth, record cursor start, enter child */
        PLG(α, NULL);
        PS(NULL,    "%s = STACK_DEPTH_fn();", stk_depth);
        PS(NULL,    "%s = %s;", start_var, cursor);
        PS(child_α, "goto %s;", child_α);

        emit_pat_node(child,
                  child_α, child_β,
                  lit_check, ω,
                  subj, subj_len, cursor, depth + 1);

        /* literal check for varname token.
         * On failure: restore cursor to before child ran (start_var) AND
         * restore @S to undo any Shift() calls the child made, THEN jump
         * to ω — caller's ALT wiring needs clean cursor+stack state. */

        int litlen = (int)strlen(varname);
        PLG(lit_check, NULL);
        PS(NULL, "if (%s + %d > %s)               { %s = %s; while(STACK_DEPTH_fn()>%s) pop_val(); goto %s; }", cursor, litlen, subj_len, cursor, start_var, stk_depth, ω);
        for (int ci = 0; ci < litlen; ci++)
            PS(NULL, "if (%s[%s + %d] != '%c')             { %s = %s; while(STACK_DEPTH_fn()>%s) pop_val(); goto %s; }",
               subj, cursor, ci, varname[ci], cursor, start_var, stk_depth, ω);
        PS(NULL, "%s = %s; %s += %d;", lit_saved, cursor, cursor, litlen);
        PS(do_assign, "goto %s;", do_assign);

        label_fmt(lit_check_β, "dlit", uid, "β2");
        PLG(lit_check_β, NULL);
        PS(NULL, "%s = %s;", cursor, lit_saved);
        PS(ω, "goto %s;", ω);

        /* β: backtrack into child */
        PLG(β, child_β);

        /* do_assign: capture child-span → discard ('_'), then γ */
        PLG(do_assign, NULL);
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = (char*)GC_malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        PS(NULL,  "  NV_SET_fn(\"_\", STRVAL(_os));");
        PS(γ, "}");
        return;
    }

    /* do_assign: capture span → variable, then γ */
    /* α: record start, enter child (non-literal-tok normal path) */
    PL(α, child_α, "%s = %s;", start_var, cursor);

    emit_pat_node(child,
              child_α, child_β,
              do_assign, ω,
              subj, subj_len, cursor, depth + 1);

    /* do_assign: capture span → variable, then γ */
    PLG(do_assign, NULL);
    if (strcasecmp(varname, "OUTPUT") == 0) {
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = (char*)GC_malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        if (suppress_output_in_named_pat()) {
            /* $-capture inside a named pattern fires during parse traversal.
             * Gen/pp handles all actual output — suppress output_str here. */
            PS(NULL,  "  (void)_os; /* output_str suppressed in named pat: Gen handles pp output */");
        } else {
            PS(NULL,  "  output_str(_os);");
        }
        PS(γ, "  free(_os); }");
    } else {
        /* Sync both hash table and C static (if one exists).
         * Skip C static sync for '_' (discard var → '__' invalid) and
         * any name that maps to a reserved/invalid C identifier.
         * When do_shift=1 (~ operator), varname is a tree tag not a SNOBOL4
         * variable — never emit C static assignment for tags. */
        int skip_cstatic = (strcmp(varname, "_") == 0) || do_shift || (varname[0] == '\0');
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = (char*)GC_malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        if (skip_cstatic) {
            PS(NULL,  "  NV_SET_fn(\"%s\", STRVAL(_os));", varname);
        } else {
            PS(NULL,  "  NV_SET_fn(\"%s\", STRVAL(_os));", varname);
            PS(NULL,  "  %s = STRVAL(_os);", cs(varname));
        }
        if (do_shift) {
            /* ~ operator: PUSH_fn tree node onto shift-reduce stack via Shift(type, value) */
            PS(NULL, "  { DESCR_t _shift_args[2] = {STRVAL(\"%s\"), STRVAL(_os)};", varname);
            PS(NULL, "    APPLY_fn(\"Shift\", _shift_args, 2); }");
        }
        PS(γ, "}");
    }

    /* β: backtrack into child */
    PLG(β, child_β);
}

/* -----------------------------------------------------------------------
 * E_CAPT_COND (. capture) node — conditional assignment
 *
 * Captures span into a C temp var on child success (overwriting on each
 * backtrack attempt).  Registers with cond_register() so emit.c can
 * flush the actual NV_SET at _byrd_ok — the single point where the whole
 * match has definitively succeeded.
 * ----------------------------------------------------------------------- */
static void emit_cond(EXPR_t *child, const char *varname,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj, const char *subj_len,
                      const char *cursor, int depth) {
    int uid = next_uid();

    /* Sanitize varname for C identifier */
    char safe_varname[NAMED_PAT_LBUF2];
    { int i = 0; const char *s = varname;
      for (; *s && i < (int)(sizeof safe_varname)-1; s++, i++)
          safe_varname[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
      safe_varname[i] = '\0'; }
    const char *vn = safe_varname;

    Label child_α, child_β;
    label_fmt(child_α, "cond_c", uid, "α");
    label_fmt(child_β, "cond_c", uid, "β");

    char start_var[LBUF], tmp_var[LBUF], do_capture[LBUF];
    snprintf(start_var,  LBUF, "%s_cstart",  α);
    snprintf(tmp_var,    LBUF, "cond_%s_%d", vn, uid);
    snprintf(do_capture, LBUF, "%s_do_cap",  α);
    decl_add("int64_t %s", start_var);
    decl_add("char *%s",   tmp_var);

    /* Register pending conditional — flushed by emit.c at _byrd_ok */
    int has_cs = !(strcmp(vn, "_") == 0 || vn[0] == '\0' ||
                   strcasecmp(varname, "OUTPUT") == 0);
    cond_register(varname, tmp_var, has_cs);

    /* α: init tmp to NULL, record start, enter child */
    PLG(α, NULL);
    PS(NULL,     "%s = NULL;",      tmp_var);
    PS(child_α,  "%s = %s;",        start_var, cursor);

    emit_pat_node(child,
              child_α, child_β,
              do_capture, ω,
              subj, subj_len, cursor, depth + 1);

    /* do_capture: copy current span into tmp_var, then → γ.
     * On backtrack, ARB re-enters child_α, extends span, calls do_capture
     * again — tmp_var is overwritten with the longer span.  Only the value
     * present at _byrd_ok (flushed by byrd_cond_emit_assigns) matters. */
    PLG(do_capture, NULL);
    PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
    PS(NULL,  "  %s = (char*)GC_malloc(_len + 1);", tmp_var);
    PS(γ, "  memcpy(%s, %s + %s, _len); %s[_len] = '\\0'; }", tmp_var, subj, start_var, tmp_var);

    /* β: backtrack into child */
    PLG(β, child_β);
}

/* -----------------------------------------------------------------------
 * FENCE node (no-argument form)
 *
 * Succeeds once, cannot be resumed (β → ω immediately).
 * ----------------------------------------------------------------------- */

static void emit_fence(const char *α, const char *β,
                       const char *γ, const char *ω) {
    PLG(α, γ);   /* FENCE: α → γ, no backtrack */
    PLG(β,  ω);
}

/* -----------------------------------------------------------------------
 * SUCCEED node — always succeeds, repeats forever on backtrack
 * ----------------------------------------------------------------------- */

static void emit_succeed(const char *α, const char *β,
                         const char *γ) {
    PLG(α, γ);   /* SUCCEED: α and β both → γ */
    PLG(β,  γ);
}

/* -----------------------------------------------------------------------
 * DT_FAIL node — always fails
 * ----------------------------------------------------------------------- */

static void emit_fail(const char *α, const char *β,
                           const char *ω) {
    PLG(α, ω);   /* DT_FAIL: α and β both → ω */
    PLG(β,  ω);
}

/* -----------------------------------------------------------------------
 * ABORT node — terminates the entire match
 * ----------------------------------------------------------------------- */

static void emit_abort(const char *α, const char *β,
                            const char *ω) {
    PLG(α, ω);   /* TODO: propagate abort signal */
    PLG(β,  ω);
}

/* -----------------------------------------------------------------------
 * emit_simple_val — emit a DESCR_t C expression for a simple EXPR_t node.
 * Used by E_OPSYN to pass left/right as runtime values.
 * Handles: E_QLIT, E_ILIT, E_VAR, E_FNC(nTop), E_NUL/NULL.
 * Falls back to NULVCL for anything complex.
 * ----------------------------------------------------------------------- */

static void emit_simple_val(EXPR_t *e) {
    if (!e) { B("NULVCL"); return; }
    switch (e->kind) {
    case E_QLIT: {
        /* Any quoted string containing "nTop()" is a counter expression — wire to
         * C-level ntop() so the compiled Byrd box counter (NPUSH_fn/NINC_fn/NPOP_fn)
         * and Reduce agree on the stack depth at match time.
         * This covers both the simple case 'nTop()' and compound forms like
         * '*(GT(nTop(), 1) nTop())' which mean "use the current counter as count". */
        if (e->sval && strcasestr(e->sval, "nTop()"))
            { B("INTVAL(ntop())"); return; }
        /* Strip outer single-quote pair: sval "'tag'" -> emit STRVAL("tag"). */
        const char *sv = e->sval ? e->sval : "";
        size_t sl = strlen(sv);
        /* Bug6b: sval starting with '*' is a SNOBOL4 pattern expression
         * (e.g. "*(':' Brackets)") — must be EVAL'd at runtime to produce
         * the actual pattern object. ShiftReduce.sno Reduce() only calls
         * EVAL(t) when DATATYPE(t)=='EXPRESSION'; passing STRVAL gives
         * type STRING and skips EVAL, leaving the raw string as the type.
         * Wrapping with EVAL_fn() here pre-evaluates it to the pattern. */
        if (sl > 0 && sv[0] == '*') {
            B("EVAL_fn(STRVAL(\"%s\"))", sv);
            return;
        }
        if (sl >= 2 && sv[0] == '\'' && sv[sl-1] == '\'')
            B("STRVAL(\"%.*s\")", (int)(sl-2), sv+1);
        else
            B("STRVAL(\"%s\")", sv);
        return;
    }
    case E_ILIT:
        B("INTVAL(%ld)", e->ival);
        return;
    case E_VAR:
        B("NV_GET_fn(\"%s\")", e->sval ? e->sval : "");
        return;
    case E_FNC:
        if (e->sval && strcasecmp(e->sval, "nTop") == 0)
            { B("INTVAL(ntop())"); return; }
        if (e->sval && strcasecmp(e->sval, "nInc") == 0)
            { B("INTVAL((NINC_fn(), ntop()))"); return; }
        if (e->sval && strcasecmp(e->sval, "nPop") == 0)
            { B("INTVAL((NPOP_fn(), 0))"); return; }
        /* generic: fall through to APPLY_fn */
        B("APPLY_fn(\"%s\", NULL, 0)", e->sval ? e->sval : "");
        return;
    default:
        B("NULVCL /* unhandled emit_simple_val kind %d */", (int)e->kind);
        return;
    }
}

/* -----------------------------------------------------------------------
 * Main dispatch — byrd_emit
 *
 * Recursively lowers `pat` with the given four-port labels and emits C.
 * depth is used for recursion guard only.
 * ----------------------------------------------------------------------- */

static void emit_pat_node(EXPR_t *pat,
                      const char *α, const char *β,
                      const char *γ, const char *ω,
                      const char *subj, const char *subj_len,
                      const char *cursor,
                      int depth) {
    if (!pat) {
        /* epsilon: succeed immediately */
        PLG(α, γ);
        PLG(β,  ω);
        return;
    }

    if (depth > 128) {
        B("/* emit_byrd: depth limit hit */\n");
        PLG(α, ω);
        PLG(β,  ω);
        return;
    }

    switch (pat->kind) {

    /* ---------------------------------------------------------------- E_QLIT */
    case E_QLIT:
        emit_lit(pat->sval, α, β, γ, ω,
                 subj, subj_len, cursor);
        return;

    /* ---------------------------------------------------------------- E_FNC — builtins */
    case E_FNC: {
        const char *n = pat->sval;

        /* SNO_MSTART — zero-width: capture current cursor into _mstart after ARB prefix */
        if (strcmp(n, "SNO_MSTART") == 0) {
            int stmt_u = (int)pat->ival;
            char mstart_var[64];
            snprintf(mstart_var, sizeof mstart_var, "_mstart%d", stmt_u);
            PL(α, γ, "%s = %s;", mstart_var, cursor);
            PLG(β, ω);
            return;
        }

        /* LEN(n) */
        if (strcasecmp(n, "LEN") == 0 && pat->nchildren >= 1) {
            long v = (pat->children[0]->kind == E_ILIT) ? pat->children[0]->ival : 1;
            emit_len(v, α, β, γ, ω, subj_len, cursor);
            return;
        }
        /* POS(n) */
        if (strcasecmp(n, "POS") == 0 && pat->nchildren >= 1) {
            if (pat->children[0]->kind == E_ILIT) {
                emit_pos(pat->children[0]->ival, α, β, γ, ω, cursor);
            } else {
                char expr[256];
                snprintf(expr, sizeof expr, "to_int(NV_GET_fn(\"%s\"))",
                         (pat->children[0]->sval ? pat->children[0]->sval : ""));
                emit_pos_expr(expr, α, β, γ, ω, cursor);
            }
            return;
        }
        /* RPOS(n) */
        if (strcasecmp(n, "RPOS") == 0 && pat->nchildren >= 1) {
            if (pat->children[0]->kind == E_ILIT) {
                emit_rpos(pat->children[0]->ival, α, β, γ, ω, subj_len, cursor);
            } else {
                char expr[256];
                snprintf(expr, sizeof expr, "to_int(NV_GET_fn(\"%s\"))",
                         (pat->children[0]->sval ? pat->children[0]->sval : ""));
                emit_rpos_expr(expr, α, β, γ, ω, subj_len, cursor);
            }
            return;
        }
        /* TAB(n) */
        if (strcasecmp(n, "TAB") == 0 && pat->nchildren >= 1) {
            if (pat->children[0]->kind == E_ILIT) {
                emit_tab(pat->children[0]->ival, α, β, γ, ω, cursor);
            } else {
                char expr[256];
                snprintf(expr, sizeof expr, "to_int(NV_GET_fn(\"%s\"))",
                         (pat->children[0]->sval ? pat->children[0]->sval : ""));
                emit_tab_expr(expr, α, β, γ, ω, cursor);
            }
            return;
        }
        /* RTAB(n) */
        if (strcasecmp(n, "RTAB") == 0 && pat->nchildren >= 1) {
            if (pat->children[0]->kind == E_ILIT) {
                emit_rtab(pat->children[0]->ival, α, β, γ, ω, subj_len, cursor);
            } else {
                char expr[256];
                snprintf(expr, sizeof expr, "to_int(NV_GET_fn(\"%s\"))",
                         (pat->children[0]->sval ? pat->children[0]->sval : ""));
                emit_rtab_expr(expr, α, β, γ, ω, subj_len, cursor);
            }
            return;
        }
        /* ANY(cs) */
        if (strcasecmp(n, "ANY") == 0 && pat->nchildren >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->children[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_any(cs, α, β, γ, ω, subj, subj_len, cursor);
            return;
        }
        /* NOTANY(cs) */
        if (strcasecmp(n, "NOTANY") == 0 && pat->nchildren >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->children[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_notany(cs, α, β, γ, ω, subj, subj_len, cursor);
            return;
        }
        /* SPAN(cs) */
        if (strcasecmp(n, "SPAN") == 0 && pat->nchildren >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->children[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_span(cs, α, β, γ, ω, subj, subj_len, cursor);
            return;
        }
        /* BREAK(cs) */
        if (strcasecmp(n, "BREAK") == 0 && pat->nchildren >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->children[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_break(cs, α, β, γ, ω, subj, subj_len, cursor);
            return;
        }
        if (strcasecmp(n, "BREAKX") == 0 && pat->nchildren >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->children[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_breakx(cs, α, β, γ, ω, subj, subj_len, cursor);
            return;
        }
        /* ARB */
        if (strcasecmp(n, "ARB") == 0) {
            emit_arb(α, β, γ, ω, subj_len, cursor);
            return;
        }
        /* REM */
        if (strcasecmp(n, "REM") == 0) {
            emit_rem(α, β, γ, ω, subj_len, cursor);
            return;
        }
        /* ARBNO(child_pat) */
        if (strcasecmp(n, "ARBNO") == 0 && pat->nchildren >= 1) {
            emit_arbno(pat->children[0], α, β, γ, ω,
                       subj, subj_len, cursor, depth);
            return;
        }
        /* FENCE() — no arg form */
        if (strcasecmp(n, "FENCE") == 0 && pat->nchildren == 0) {
            emit_fence(α, β, γ, ω);
            return;
        }
        /* FENCE(pat) — with argument: match pat, then cut */
        if (strcasecmp(n, "FENCE") == 0 && pat->nchildren >= 1) {
            int uid = next_uid();
            Label ca, cb;
            label_fmt(ca, "fence_p", uid, "α");
            label_fmt(cb, "fence_p", uid, "β");
            char fence_after[LBUF];
            snprintf(fence_after, LBUF, "fence_after_%d", uid);

            PLG(α, ca);   /* FENCE(p): enter child */
            emit_pat_node(pat->children[0],
                      ca, cb,
                      fence_after, ω,
                      subj, subj_len, cursor, depth + 1);
            PLG(fence_after, γ);   /* child succeeded → γ (no backtrack) */
            PLG(β, ω);          /* β: cut */
            return;
        }
        /* SUCCEED */
        if (strcasecmp(n, "SUCCEED") == 0) {
            emit_succeed(α, β, γ);
            return;
        }
        /* DT_FAIL */
        if (strcasecmp(n, "DT_FAIL") == 0) {
            emit_fail(α, β, ω);
            return;
        }
        /* ABORT */
        if (strcasecmp(n, "ABORT") == 0) {
            emit_abort(α, β, ω);
            return;
        }

        /* nPush() */
        if (strcasecmp(n, "nPush") == 0) {
            int sf = pending_npush_uid;
            /* Do NOT consume pending_npush_uid here — E_OPSYN & will consume it.
             * We only read it to emit the NTOP_INDEX_fn() save alongside NPUSH_fn(). */
            if (sf >= 0) {
                /* Bug5: save frame index so Reduce can use NSTACK_AT_fn.
                 * Emit bare field name — decl_emit_defines provides z-> prefix. */
                PL(α, γ, "NPUSH_fn(); _saved_frame_%d = NTOP_INDEX_fn();", sf);
            } else {
                PL(α, γ, "NPUSH_fn();");
            }
            PLG(β, ω);
            return;
        }
        /* nInc() */
        if (strcasecmp(n, "nInc") == 0) {
            /* Always use NINC_fn() — increments _ntop (current frame top).
             * Bug5 fix (NSTACK_AT_fn) applies only at Reduce time, not here. */
            PL(α, γ, "NINC_fn();");
            PL(β, ω,  "NDEC_fn();");
            return;
        }
        /* nDec() */
        if (strcasecmp(n, "nDec") == 0) {
            PL(α, γ, "NDEC_fn();");
            PLG(β, ω);
            return;
        }
        /* nPop() */
        if (strcasecmp(n, "nPop") == 0) {
            PL(α, γ, "NPOP_fn();");
            PLG(β, ω);
            return;
        }
        /* nTop() */
        if (strcasecmp(n, "nTop") == 0) {
            PL(α, γ, "(void)ntop();");
            PLG(β, ω);
            return;
        }

        /* Fallback: user-defined or unrecognised function call in pattern context.
         * Build the argument list at runtime, call APPLY_fn, check for failure.
         * If APPLY_fn returns FAILDESCR → pattern fails (goto ω).
         * If it returns any non-fail value → pattern succeeds (goto γ), cursor unchanged.
         * β → ω: function calls are non-resumable (no backtrack alternative). */
        {
            int uid = next_uid();
            PLG(α, NULL);
            /* Build args array */
            int nargs = pat->nchildren;
            if (nargs > 0) {
                PS(NULL, "{ DESCR_t _fcall_%d_args[%d];", uid, nargs);
                for (int ai = 0; ai < nargs; ai++) {
                    EXPR_t *a = pat->children[ai];
                    if (a && a->kind == E_VAR && a->sval)
                        PS(NULL, "  _fcall_%d_args[%d] = NV_GET_fn(\"%s\");", uid, ai, a->sval);
                    else if (a && a->kind == E_QLIT && a->sval)
                        PS(NULL, "  _fcall_%d_args[%d] = STRVAL(\"%s\");", uid, ai, a->sval);
                    else
                        PS(NULL, "  _fcall_%d_args[%d] = NULVCL;", uid, ai);
                }
                PS(NULL, "  DESCR_t _fcall_%d_r = APPLY_fn(\"%s\", _fcall_%d_args, %d);",
                   uid, n, uid, nargs);
                PS(γ, "  if (IS_FAIL_fn(_fcall_%d_r)) goto %s; }", uid, ω);
            } else {
                PS(NULL, "{ DESCR_t _fcall_%d_r = APPLY_fn(\"%s\", NULL, 0);", uid, n);
                PS(γ, "  if (IS_FAIL_fn(_fcall_%d_r)) goto %s; }", uid, ω);
            }
            PLG(β, ω);
            return;
        }
    }

    /* ----------------------------------------------------------- E_SEQ (pattern CAT / Byrd-box SEQ) */
    case E_SEQ: {
        int _nc = pat->nchildren;
        if (_nc == 0) { PLG(α, γ); return; }
        if (_nc == 1) { emit_pat_node(pat->children[0], α, β, γ, ω, subj, subj_len, cursor, depth); return; }
        if (_nc == 2) {
            emit_seq(pat->children[0], pat->children[1], α, β, γ, ω, subj, subj_len, cursor, depth);
            return;
        }
        /* >2: right-fold into heap-allocated binary nodes, then emit root */
        EXPR_t **_nodes = malloc((size_t)(_nc - 1) * sizeof(EXPR_t *));
        EXPR_t **_kids  = malloc((size_t)(_nc - 1) * 2 * sizeof(EXPR_t *));
        EXPR_t *_right = pat->children[_nc - 1];
        for (int _i = _nc - 2; _i >= 0; _i--) {
            int _n = _nc - 2 - _i;
            _nodes[_n] = calloc(1, sizeof(EXPR_t));
            _nodes[_n]->kind = E_SEQ;
            _kids[_n*2+0] = pat->children[_i];
            _kids[_n*2+1] = _right;
            _nodes[_n]->children  = &_kids[_n*2];
            _nodes[_n]->nchildren = 2;
            _right = _nodes[_n];
        }
        emit_pat_node(_right, α, β, γ, ω, subj, subj_len, cursor, depth);
        for (int _i = 0; _i < _nc - 1; _i++) free(_nodes[_i]);
        free(_nodes); free(_kids);
        return;
    }

    /* ---------------------------------------------------------------- E_ALT */
    case E_ALT: {
        int _nc = pat->nchildren;
        if (_nc == 0) { PLG(α, ω); return; }
        if (_nc == 1) { emit_pat_node(pat->children[0], α, β, γ, ω, subj, subj_len, cursor, depth); return; }
        if (_nc == 2) {
            emit_alt(pat->children[0], pat->children[1], α, β, γ, ω, subj, subj_len, cursor, depth);
            return;
        }
        /* >2: right-fold into heap-allocated binary nodes */
        EXPR_t **_nodes = malloc((size_t)(_nc - 1) * sizeof(EXPR_t *));
        EXPR_t **_kids  = malloc((size_t)(_nc - 1) * 2 * sizeof(EXPR_t *));
        EXPR_t *_right = pat->children[_nc - 1];
        for (int _i = _nc - 2; _i >= 0; _i--) {
            int _n = _nc - 2 - _i;
            _nodes[_n] = calloc(1, sizeof(EXPR_t));
            _nodes[_n]->kind = E_ALT;
            _kids[_n*2+0] = pat->children[_i];
            _kids[_n*2+1] = _right;
            _nodes[_n]->children  = &_kids[_n*2];
            _nodes[_n]->nchildren = 2;
            _right = _nodes[_n];
        }
        emit_pat_node(_right, α, β, γ, ω, subj, subj_len, cursor, depth);
        for (int _i = 0; _i < _nc - 1; _i++) free(_nodes[_i]);
        free(_nodes); free(_kids);
        return;
    }

    /* ---------------------------------------------------------------- E_CAPT_IMM ($ assign) */
    case E_CAPT_IMM: {
        /* Normal case: right is a plain variable or quoted literal → direct capture. */
        if (!pat->children[1] ||
            pat->children[1]->kind == E_VAR ||
            pat->children[1]->kind == E_QLIT) {
            const char *varname = "OUTPUT";
            if (pat->children[1] && pat->children[1]->sval)
                varname = pat->children[1]->sval;
            emit_imm(pat->children[0], varname,
                     α, β, γ, ω,
                     subj, subj_len, cursor, depth, 0);
            return;
        }

        /* Computed-right case: right is not a plain var/lit.
         * With left-assoc $ parsing, "SPAN $ tx $ *match(...)" gives:
         *   E_CAPT_IMM(E_CAPT_IMM(SPAN, tx), *match(...))
         * The inner E_CAPT_IMM(SPAN,tx) does the real capture.
         * The outer right (*match(...)) is a zero-width filter.
         *
         * Label discipline (learned from emit_seq):
         *   PLG(β, right_b)  -- define β BEFORE recursion, points to right_b
         *   PLG(α, left_a)  -- define α BEFORE recursion, points to left_a
         *   emit_pat_node(left,  left_a, left_b,  mid_ok, ω)
         *   emit_pat_node(right, mid_ok, right_b, γ,  ω)
         *
         * β:   defined once here (goto right_b).
         * left_a, left_b, right_b: each defined exactly once by their recursion.
         * mid_ok: defined once by the left arm (it is left arm's γ).
         * No label is PLG-emitted more than once.
         *
         * Semantics:
         *   On outer backtrack (β): resume right arm via right_b.
         *   Right arm fails: goes to ω (no further backtrack).
         *   Left arm fails: goes to ω.
         */
        int uid = next_uid();
        Label left_a, left_b, right_b, mid_ok;
        label_fmt(left_a,  "dolc", uid, "la");
        label_fmt(left_b,  "dolc", uid, "lb");
        label_fmt(right_b, "dolc", uid, "rb");
        label_fmt(mid_ok,  "dolc", uid, "mid");

        PLG(α, left_a);
        PLG(β,  right_b);

        emit_pat_node(pat->children[0],
                  left_a, left_b,
                  mid_ok, ω,
                  subj, subj_len, cursor, depth + 1);

        emit_pat_node(pat->children[1],
                  mid_ok, right_b,
                  γ, ω,
                  subj, subj_len, cursor, depth + 1);

        return;
    }

    /* --------------------------------------------------------------- E_CAPT_COND (. assign OR ~ shift) */
    case E_CAPT_COND: {
        const char *varname = "OUTPUT";
        if (pat->children[1] && (pat->children[1]->kind == E_VAR || pat->children[1]->kind == E_QLIT))
            varname = pat->children[1]->sval;
        /* Distinguish ~ (shift) from . (conditional name capture):
         * ~ right-hand side is always E_QLIT (a quoted tag like '=', 'Label', etc.)
         * . right-hand side is always E_VAR (a plain variable name)
         * When tag is E_QLIT, route to emit_imm with do_shift=1 to call Shift(). */
        if (pat->children[1] && pat->children[1]->kind == E_QLIT) {
            emit_imm(pat->children[0], varname,
                     α, β, γ, ω,
                     subj, subj_len, cursor, depth, 1);
        } else {
            emit_cond(pat->children[0], varname,
                      α, β, γ, ω,
                      subj, subj_len, cursor, depth);
        }
        return;
    }

    /* --------------------------------------------------------------- E_CAPT_CUR (@ position) */
    case E_CAPT_CUR: {
        /* @VAR — zero-width: capture current cursor position as integer into VAR.
         * Always succeeds, never consumes input.  No backtrack effect. */
        /* unary @VAR: parser puts operand in left (via unop()).
         * binary @VAR (rare): right holds the variable.
         * Prefer left->sval, fall back to right->sval. */
        const char *varname =
            (pat->children[0]  && pat->children[0]->sval)  ? pat->children[0]->sval  :
            (pat->children[1] && pat->children[1]->sval) ? pat->children[1]->sval : "_";
        char safe[NAMED_PAT_LBUF2];
        { int i=0; const char *s=varname;
          for(;*s&&i<(int)sizeof(safe)-1;s++,i++)
              safe[i]=(isalnum((unsigned char)*s)||*s=='_')?*s:'_';
          safe[i]='\0'; }
        /* α: assign cursor position to var, → γ */
        PLG(α, NULL);
        PS(NULL, "{ NV_SET_fn(\"%s\", INTVAL_fn((int64_t)%s));", varname, cursor);
        int skip_cs = (strcmp(safe,"_")==0||safe[0]=='\0');
        if (!skip_cs)
            PS(NULL, "  %s = INTVAL_fn((int64_t)%s);", cs(varname), cursor);
        PS(γ, "}");
        /* β → ω: @VAR is zero-width (no state to restore) but non-resumable.
         * On backtrack, propagate failure upward — do NOT re-enter γ. */
        PLG(β, ω);
        return;
    }

    /* ---------------------------------------------------------------- E_VAR (pattern var)
     * A bare variable name in pattern context means implicit deref — same as *X.
     * Fall through to E_INDR with varname = pat->sval.
     * (Previously emitted epsilon — wrong: `nl` in Command would silently match nothing.) */
    case E_VAR: {
        const char *varname = pat->sval ? pat->sval : "";

        /* Zero-arg builtin patterns used as bare names (no parens).
         * REM, ARB, FAIL, SUCCEED, FENCE, ABORT are valid pattern literals
         * in SNOBOL4 — they appear without () and must not be treated as
         * variable dereferences.  Route them through the same emitters as
         * the E_FNC path so behaviour is identical. */
        if (strcasecmp(varname, "REM") == 0) {
            emit_rem(α, β, γ, ω, subj_len, cursor);
            return;
        }
        if (strcasecmp(varname, "ARB") == 0) {
            emit_arb(α, β, γ, ω, subj_len, cursor);
            return;
        }
        if (strcasecmp(varname, "FAIL") == 0 || strcasecmp(varname, "DT_FAIL") == 0) {
            emit_fail(α, β, ω);
            return;
        }
        if (strcasecmp(varname, "SUCCEED") == 0) {
            emit_succeed(α, β, γ);
            return;
        }
        if (strcasecmp(varname, "FENCE") == 0) {
            emit_fence(α, β, γ, ω);
            return;
        }
        if (strcasecmp(varname, "ABORT") == 0) {
            emit_abort(α, β, ω);
            return;
        }

        const NamedPat *np_v = named_pat_lookup(varname);
        if (np_v) {
            /* Compiled named pattern — direct call */
            int uid = next_uid();
            char saved_cur[LBUF];
            snprintf(saved_cur, LBUF, "deref_%d_saved_cur", uid);
            decl_add("int64_t %s", saved_cur);
            char child_field[LBUF];
            snprintf(child_field, LBUF, "deref_%d_z", uid);
            {
                char child_decl[DECL_LINE_MAX];
                snprintf(child_decl, DECL_LINE_MAX, "%s *%s", np_v->typename, child_field);
                if (in_named_pat)
                    child_decl_add(child_decl);
                else
                    decl_add("%s *%s", np_v->typename, child_field);
            }
            /* Bug6a: inside pat_X4, before attempting another recursive *X4 call,
             * reject ':' as a valid atom start — it belongs to pat_Goto, not a
             * concat expression atom.  Without this, FENCE(*White *X4) in pat_X4
             * consumes the space before ':(END)' then tries X4 on ':(END)', producing
             * a spurious Reduce(..,2) that misaligns the Stmt parse tree. */
            int x4_colon_guard = (strcasecmp(varname, "X4") == 0 && in_named_pat);

            /* Bug5: cross-pattern nInc frame threading.
             * If pending_npush_uid is set, the enclosing emit_seq has a nPush()
             * left child whose frame index is saved in _saved_frame_UID.
             * Before calling a child named pattern that contains nInc(), set
             * _pending_parent_frame so the child can use NINC_AT_fn. */
            int pf_uid = pending_npush_uid;
            /* Don't consume pending_npush_uid here — E_OPSYN will consume it.
             * But do thread it to child. */

            B("%s: {\n", α);
            if (x4_colon_guard)
                B("    /* Bug6a: ':' after whitespace belongs to pat_Goto */\n"
                  "    if (%s < %s && %s[%s] == ':') { goto %s; }\n",
                  cursor, subj_len, subj, cursor, ω);
            if (pf_uid >= 0)
                B("    _pending_parent_frame = _saved_frame_%d;\n", pf_uid);
            B("    %s = %s;\n", saved_cur, cursor);
            B("    DESCR_t _r_%d = %s(%s, %s, &%s, &%s, 0);\n",
              uid, np_v->fnname, subj, subj_len, cursor, child_field);
            B("    if (IS_FAIL_fn(_r_%d)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, ω);
            B("    goto %s;\n", γ);
            B("}\n");
            B("%s: {\n", β);
            if (x4_colon_guard)
                B("    /* Bug6a: same guard on backtrack path */\n"
                  "    if (%s < %s && %s[%s] == ':') { goto %s; }\n",
                  cursor, subj_len, subj, cursor, ω);
            if (pf_uid >= 0)
                B("    _pending_parent_frame = _saved_frame_%d;\n", pf_uid);
            B("    %s = %s;\n", cursor, saved_cur);
            B("    DESCR_t _r_%d_b = %s(%s, %s, &%s, &%s, 1);\n",
              uid, np_v->fnname, subj, subj_len, cursor, child_field);
            B("    if (IS_FAIL_fn(_r_%d_b)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, ω);
            B("    goto %s;\n", γ);
            B("}\n");
        } else {
            /* String/dynamic pattern — match_pattern_at fallback */
            char saved[LBUF];
            snprintf(saved, LBUF, "deref_%d_saved_cursor", next_uid());
            decl_add("int64_t %s", saved);
            B("%s: {\n", α);
            B("    DESCR_t _deref_pat = NV_GET_fn(\"%s\");\n", varname);
            B("    int _deref_new_cur = match_pattern_at(_deref_pat, %s, (int)%s, (int)%s);\n",
              subj, subj_len, cursor);
            B("    if (_deref_new_cur < 0) goto %s;\n", ω);
            B("    %s = %s;\n", saved, cursor);
            B("    %s = (int64_t)_deref_new_cur;\n", cursor);
            B("    goto %s;\n", γ);
            B("}\n");
            B("%s:\n", β);
            B("    %s = %s;\n", cursor, saved);
            B("    goto %s;\n", ω);
        }
        return;
    }

    /* ---------------------------------------------------------------- E_INDR (deferred ref) */
    case E_INDR: {
        const char *varname = NULL;
        /* Grammar: unary *X  → E_INDR(left=NULL, right=E_VAR("X"))
         * Also seen: left=E_VAR in some paths — check both.
         * Also: left=E_FNC("name") — *name() where name is a named pattern fn.
         * Also: left=NULL, right=E_QLIT("x") — unary $'x' output capture. */
        if (pat->children[1] && pat->children[1]->kind == E_VAR)
            varname = pat->children[1]->sval;
        else if (pat->children[0] && pat->children[0]->kind == E_VAR)
            varname = pat->children[0]->sval;
        else if (pat->children[0] && pat->children[0]->kind == E_FNC && pat->children[0]->sval)
            varname = pat->children[0]->sval;
        if (!varname) varname = "";

        /* -------------------------------------------------------------------
         * Category: unary $'literal' — E_INDR(left=NULL, right=E_QLIT("x"))
         * Semantics: match the literal, capture matched span to OUTPUT.
         * ------------------------------------------------------------------- */
        if (!pat->children[0] && pat->children[1] && pat->children[1]->kind == E_QLIT && pat->children[1]->sval) {
            const char *lit = pat->children[1]->sval;
            int uid2 = next_uid();
            Label lit_α, lit_β;
            label_fmt(lit_α, "dlit", uid2, "α");
            label_fmt(lit_β, "dlit", uid2, "β");
            char start_var[LBUF], do_assign[LBUF];
            snprintf(start_var, LBUF, "dlit_%d_start", uid2);
            snprintf(do_assign, LBUF, "dlit_%d_do_assign", uid2);
            decl_add("int64_t %s", start_var);

            /* α: record start, match literal */
            PLG(α, NULL);
            PS(NULL, "%s = %s;", start_var, cursor);
            PS(lit_α, "goto %s;", lit_α);

            emit_lit(lit, lit_α, lit_β, do_assign, ω,
                     subj, subj_len, cursor);

            /* do_assign: capture span → OUTPUT, then γ */
            PLG(do_assign, NULL);
            PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
            PS(NULL,  "  char *_os = (char*)GC_malloc(_len + 1);");
            PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
            if (suppress_output_in_named_pat()) {
                PS(NULL,  "  (void)_os; /* output_str suppressed in named pat */");
                PS(γ, "  free(_os); }");
            } else {
                PS(γ, "  output_str(_os); free(_os); }");
            }

            PLG(β, lit_β);
            return;
        }

        /* -------------------------------------------------------------------
         * Category: *fnname(args...) — E_INDR(left=E_FNC("name",[...]))
         *
         * Semantics: call the function with its arguments to obtain a pattern
         * descriptor at runtime, then use THAT descriptor as the pattern.
         * This is the correct reading of SNOBOL4 *f(a,b): evaluate f(a,b),
         * treat the result as an unevaluated (deferred) pattern.
         *
         * The bug (session 107): line 1951 above extracted only pat->children[0]->sval
         * ("match") and fell through to NV_GET_fn("match") — a variable lookup
         * by name, dropping ALL arguments.  match_pattern_at(NULVCL,...) then
         * succeeded vacuously, making OUTPUT look like a Function.
         *
         * Fix: when the inner node is E_FNC with arguments, emit APPLY_fn(name,
         * args, nargs) to get the real pattern descriptor, then match against it.
         * β → ω: function calls are non-resumable.
         * ------------------------------------------------------------------- */
        if (pat->children[0] && pat->children[0]->kind == E_FNC && pat->children[0]->nchildren > 0) {
            EXPR_t *fn = pat->children[0];
            const char *fname = fn->sval ? fn->sval : "";
            int nargs = fn->nchildren;
            int uid = next_uid();
            char saved[LBUF];
            snprintf(saved, LBUF, "deref_fnc_%d_saved_cursor", uid);
            decl_add("int64_t %s", saved);

            B("%s: {\n", α);
            /* Build args array */
            B("    DESCR_t _fcall_%d_args[%d];\n", uid, nargs);
            for (int ai = 0; ai < nargs; ai++) {
                EXPR_t *a = fn->children[ai];
                if (a && a->kind == E_VAR && a->sval)
                    B("    _fcall_%d_args[%d] = NV_GET_fn(\"%s\");\n", uid, ai, a->sval);
                else if (a && a->kind == E_QLIT && a->sval)
                    B("    _fcall_%d_args[%d] = STRVAL(\"%s\");\n", uid, ai, a->sval);
                else if (a && a->kind == E_ILIT)
                    B("    _fcall_%d_args[%d] = INTVAL(%ld);\n", uid, ai, a->ival);
                else
                    B("    _fcall_%d_args[%d] = NULVCL;\n", uid, ai);
            }
            B("    DESCR_t _fcall_%d_pat = APPLY_fn(\"%s\", _fcall_%d_args, %d);\n",
              uid, fname, uid, nargs);
            B("    if (IS_FAIL_fn(_fcall_%d_pat)) goto %s;\n", uid, ω);
            B("    int _deref_fnc_%d_new_cur = match_pattern_at(_fcall_%d_pat, %s, (int)%s, (int)%s);\n",
              uid, uid, subj, subj_len, cursor);
            B("    if (_deref_fnc_%d_new_cur < 0) goto %s;\n", uid, ω);
            B("    %s = %s;\n", saved, cursor);
            B("    %s = (int64_t)_deref_fnc_%d_new_cur;\n", cursor, uid);
            B("    goto %s;\n", γ);
            B("}\n");
            B("%s:\n", β);
            B("    %s = %s;\n", cursor, saved);
            B("    goto %s;\n", ω);
            return;
        }

        const NamedPat *np = named_pat_lookup(varname);

        if (np) {
            /* Compiled path: direct function call — no engine.c */
            int uid = next_uid();
            char saved_cur[LBUF];
            snprintf(saved_cur, LBUF, "deref_%d_saved_cur", uid);
            decl_add("int64_t %s", saved_cur);

            char child_field[LBUF];
            snprintf(child_field, LBUF, "deref_%d_z", uid);
            {
                char child_decl[DECL_LINE_MAX];
                snprintf(child_decl, DECL_LINE_MAX, "%s *%s", np->typename, child_field);
                if (in_named_pat)
                    child_decl_add(child_decl);
                else
                    decl_add("%s *%s", np->typename, child_field);
            }

            /* α: first call — entry 0 */
            B("%s: {\n", α);
            B("    %s = %s;\n", saved_cur, cursor);
            B("    DESCR_t _r_%d = %s(%s, %s, &%s, &%s, 0);\n",
              uid, np->fnname, subj, subj_len, cursor, child_field);
            B("    if (IS_FAIL_fn(_r_%d)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, ω);
            B("    goto %s;\n", γ);
            B("}\n");

            /* β: backtrack — entry 1 */
            B("%s: {\n", β);
            B("    %s = %s;\n", cursor, saved_cur);
            B("    DESCR_t _r_%d_b = %s(%s, %s, &%s, &%s, 1);\n",
              uid, np->fnname, subj, subj_len, cursor, child_field);
            B("    if (IS_FAIL_fn(_r_%d_b)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, ω);
            B("    goto %s;\n", γ);
            B("}\n");
            return;
        }

        /* Interpreter fallback for dynamic/EVAL patterns */
        char saved[LBUF];
        snprintf(saved, LBUF, "deref_%d_saved_cursor", next_uid());
        decl_add("int64_t %s", saved);

        B("%s: {\n", α);
        B("    DESCR_t _deref_pat = NV_GET_fn(\"%s\");\n", varname);
        B("    int _deref_new_cur = match_pattern_at(_deref_pat, %s, (int)%s, (int)%s);\n",
          subj, subj_len, cursor);
        B("    if (_deref_new_cur < 0) goto %s;\n", ω);
        B("    %s = %s;\n", saved, cursor);
        B("    %s = (int64_t)_deref_new_cur;\n", cursor);
        B("    goto %s;\n", γ);
        B("}\n");
        B("%s:\n", β);
        B("    %s = %s;\n", cursor, saved);
        B("    goto %s;\n", ω);
        return;
    }

    /* --------------------------------------------------------------- E_OPSYN (& operator) */
    case E_OPSYN: {
        B("%s: /* E_OPSYN & */\n", α);

        /* Bug5: if the enclosing emit_seq saved a frame index (nPush() left),
         * use NSTACK_AT_fn(_saved_frame_UID) for the n-argument instead of ntop().
         * ntop() would give the wrong count when nested NPUSHes displace _ntop. */
        int sf_uid = pending_npush_uid;
        pending_npush_uid = -1;  /* consume — one-shot */

        /* If the n-argument is a conditional nTop() expression like
         * '*(GT(nTop(), 1) nTop())', only call Reduce when count > 1.
         * When count==1 the single item is already on the stack — no wrapping needed.
         * For plain integer literals or 'nTop()' alone, always call Reduce. */
        int conditional_ntop = (pat->children[1] && pat->children[1]->kind == E_QLIT
                                && pat->children[1]->sval
                                && strcasestr(pat->children[1]->sval, "GT(nTop()"));
        if (conditional_ntop) {
            if (sf_uid >= 0)
                B("    { int _cnt_%d = (_saved_frame_%d >= 0)"
                  " ? (int)NSTACK_AT_fn(_saved_frame_%d) : 0;\n"
                  "    if (_cnt_%d > 1) {\n", sf_uid, sf_uid, sf_uid, sf_uid);
            else
                B("    if (ntop() > 1) {\n");
        }

        /* Bug6b: if left is an E_QLIT starting with '*' (unevaluated SNOBOL4
         * pattern expression like "*(':' Brackets)"), it must be built at
         * runtime from the NV variables Brackets/SorF that pat_Target/SGoto/
         * FGoto set.  Detect the two known forms:
         *   "*(':' Brackets)"          → CONCAT_fn(STRVAL_fn(":"), NV_GET_fn("Brackets"))
         *   "*(':' SorF Brackets)"     → CONCAT_fn(STRVAL_fn(":"), CONCAT_fn(NV_GET_fn("SorF"), NV_GET_fn("Brackets")))
         * For any other '*'-prefixed literal, fall back to EVAL_fn (existing). */
        int bug6b = 0;
        if (pat->children[0] && pat->children[0]->kind == E_QLIT && pat->children[0]->sval
            && pat->children[0]->sval[0] == '*') {
            const char *sv = pat->children[0]->sval;
            if (strcasestr(sv, "SorF") && strcasestr(sv, "Brackets")) {
                B("    { DESCR_t _type_b = CONCAT_fn(STRVAL_fn(\":\"),"
                  " CONCAT_fn(NV_GET_fn(\"SorF\"), NV_GET_fn(\"Brackets\")));\n");
                bug6b = 2;
            } else if (strcasestr(sv, "Brackets")) {
                B("    { DESCR_t _type_b = CONCAT_fn(STRVAL_fn(\":\"),"
                  " NV_GET_fn(\"Brackets\"));\n");
                bug6b = 1;
            }
        }

        if (bug6b) {
            B("    { DESCR_t _reduce_args[2] = {_type_b, ");
            emit_simple_val(pat->children[1]);
            B("};\n");
            B("      APPLY_fn(\"Reduce\", _reduce_args, 2); } }\n");
        } else {
            B("    { DESCR_t _reduce_args[2] = {");
            emit_simple_val(pat->children[0]);
            B(", ");
            if (sf_uid >= 0 && conditional_ntop)
                B("INTVAL(_cnt_%d)", sf_uid);
            else if (sf_uid >= 0)
                B("INTVAL((_saved_frame_%d >= 0)"
                  " ? (int)NSTACK_AT_fn(_saved_frame_%d) : 0)", sf_uid, sf_uid);
            else
                emit_simple_val(pat->children[1]);
            B("};\n");
            B("      APPLY_fn(\"Reduce\", _reduce_args, 2); }\n");
        }

        if (conditional_ntop) {
            if (sf_uid >= 0)
                B("    } }\n");  /* close if + block for _cnt_ */
            else
                B("    }\n");
        }
        B("    goto %s;\n", γ);
        B("%s: goto %s;\n", β, ω);
        return;
    }

    /* ---------------------------------------------------------------- default */
    default:
        PLG(α, γ);   /* unhandled — epsilon */
        PLG(β,  ω);
        return;
    }
}

/* =======================================================================
 * Public API
 *
 * byrd_emit_pattern — emit a complete standalone MATCH_fn block:
 *
 *   goto <root>_α;
 *   [declarations]
 *   [Byrd box labeled-goto C]
 *   <γ_label>:   (MATCH_fn succeeded)
 *   <ω_label>:   (MATCH_fn failed)
 *
 * Called from emit.c in the pattern-MATCH_fn statement case.
 *
 * Parameters:
 *   pat          — the pattern EXPR_t* (subject pattern field)
 *   out          — output FILE*
 *   root_name    — prefix for root labels (e.g. "root_1")
 *   subject_var  — C expression for the subject string pointer
 *   subj_len_var — C expression for subject length (int64_t)
 *   cursor_var   — C lvalue for the cursor (int64_t, modified in place)
 *   γ_label  — C label to jump to on MATCH_fn success
 *   ω_label  — C label to jump to on MATCH_fn failure
 * ======================================================================= */

static void emit_pattern(EXPR_t *pat, FILE *out_file,
                       const char *root_name,
                       const char *subject_var,
                       const char *subj_len_var,
                       const char *cursor_var,
                       const char *γ_label,
                       const char *ω_label) {
    out = out_file;
    /* Save counter before first (declaration) pass so second (code) pass
     * uses the same uid sequence — never reset to 0 so multiple patterns
     * in the same compilation unit never collide. */
    int uid_saved = uid_ctr;
    decl_reset();

    /* Root labels */
    char root_α[LBUF], root_β[LBUF];
    snprintf(root_α, LBUF, "%s_α", root_name);
    snprintf(root_β,  LBUF, "%s_β",  root_name);

    /* First pass: collect declarations by running emit into /dev/null,
     * then second pass: emit the real code.
     *
     * Simpler approach used by the Python version: emit declarations
     * inline at the start using a two-buffer approach.
     *
     * We use a temporary FILE* for the code body, write declarations
     * first into out_file, then splice code. */

    /* Collect code into a temp buffer */
    char   *code_buf  = NULL;
    size_t  code_size = 0;
    FILE   *code_file = open_memstream(&code_buf, &code_size);
    if (!code_file) {
        fprintf(out_file, "/* emit_byrd: open_memstream failed */\n");
        return;
    }

    out = code_file;
    uid_ctr = uid_saved;  /* rewind to same start for code pass */
    decl_reset();

    /* Emit root entry */
    fprintf(code_file, "\n/* ===== pattern: %s ===== */\n", root_name);

    /* Lower the pattern — byrd_emit emits both α and β labels */
    emit_pat_node(pat,
              root_α, root_β,
              γ_label, ω_label,
              subject_var, subj_len_var, cursor_var,
              0);

    /* Note: root_β is emitted by byrd_emit above for all node types.
     * Do NOT emit it again here — that causes duplicate label errors. */

    fflush(code_file);
    fclose(code_file);

    /* Now emit to out_file: declarations FIRST, then goto, then code.
     * C99 forbids jumping over variable-length declarations, so all
     * static decls must appear before the first goto in the function. */
    out = out_file;
    decl_flush();            /* static declarations — before any goto */
    B("    goto %s;\n", root_α);

    /* Splice code body */
    if (code_buf && code_size > 0)
        fwrite(code_buf, 1, code_size, out_file);

    free(code_buf);
}

/* =======================================================================
 * byrd_emit_named_pattern — emit a named pattern as a compiled C function.
 *
 * Emits a forward decl + C function:
 *   static DESCR_t pat_X(const char *_subj_np, int64_t _slen_np,
 *                       int64_t *_cur_ptr_np, int _entry_np);
 * entry 0 = α (fresh), entry 1 = β (resume).
 * On success updates *_cur_ptr_np and returns SNO_EMPTY.
 * On failure returns FAILDESCR.
 *
 * Registers the name so E_INDR (*varname) emits a direct call.
 * ======================================================================= */

static void emit_named_pattern(const char *varname, EXPR_t *pat, FILE *out_file) {
    /* C-safe name */
    char safe[NAMED_PAT_LBUF2];
    {
        const char *s = varname;
        int i = 0;
        for (; *s && i < (int)(sizeof safe)-1; s++, i++)
            safe[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
        safe[i] = '\0';
    }

    char fnname[NAMED_PAT_LBUF2];
    char tyname[NAMED_PAT_LBUF2];
    snprintf(fnname, sizeof fnname, "pat_%s", safe);
    snprintf(tyname, sizeof tyname, "pat_%s_t", safe);

    named_pat_register(varname, tyname, fnname);

    /* Deduplicate: only emit each named pattern once (first definition wins).
     * beauty.sno assigns the same variable in multiple function bodies;
     * we compile the first occurrence and ignore subsequent ones. */
    NamedPat *np_entry = NULL;
    for (int i = 0; i < named_pat_count; i++) {
        if (strcmp(named_pats[i].varname, varname) == 0) {
            np_entry = &named_pats[i];
            break;
        }
    }
    if (np_entry && np_entry->emitted) return;
    if (np_entry) np_entry->emitted = 1;

    /* Reset fn_seen so statics aren't skipped due to a previous pattern's decls */
    fn_scope_reset();

    /* NOTE: forward declaration is NOT emitted here.
     * Caller must emit all forward decls (via byrd_emit_named_fwdecls) BEFORE
     * calling this function, so mutual/forward refs compile clean. */

    char   *code_buf  = NULL;
    size_t  code_size = 0;
    FILE   *code_file = open_memstream(&code_buf, &code_size);
    if (!code_file) {
        fprintf(out_file, "/* emit_byrd: open_memstream failed for %s */\n", varname);
        return;
    }

    int uid_saved = uid_ctr;
    decl_reset();

    char γ_lbl[LBUF], ω_lbl[LBUF];
    char root_α[LBUF], root_β[LBUF];
    snprintf(γ_lbl,  LBUF, "_%s_γ",    safe);
    snprintf(ω_lbl,  LBUF, "_%s_ω",  safe);
    snprintf(root_α, LBUF, "_%s_α", safe);
    snprintf(root_β,  LBUF, "_%s_β",  safe);

    /* Struct mode ON — decl_add will collect into struct fields,
     * child_decl_add will collect child frame pointers separately. */
    in_named_pat = 1;
    current_named_pat_name = varname;

    out = code_file;
    uid_ctr = uid_saved;
    decl_reset();
    cond_reset();   /* clear any pending conditionals from prior pattern */

    /* Bug5: every named pattern gets a _parent_frame field so nInc()
     * can use NINC_AT_fn(_parent_frame) to increment the correct frame
     * even when this pattern is called from inside a nested NPUSH context. */
    decl_add("int _parent_frame");

    emit_pat_node(pat,
              root_α, root_β,
              γ_lbl, ω_lbl,
              "_subj_np", "_slen_np", "_cur_np",
              0);

    fflush(code_file);
    fclose(code_file);

    in_named_pat = 0;
    current_named_pat_name = NULL;

    /* 1. Emit the struct typedef (uses decl_buf + child_decl_buf collected above) */
    decl_flush_as_struct(out_file, tyname);

    /* 2. Emit the function with new signature: pat_X(subj, slen, cur_ptr, **zz, entry) */
    fprintf(out_file,
        "static DESCR_t %s(const char *_subj_np, int64_t _slen_np,\n"
        "                  int64_t *_cur_ptr_np, %s **_zz_np, int _entry_np) {\n"
        "    if (_entry_np == 0) {\n"
        "        *_zz_np = calloc(1, sizeof(%s));\n"
        "        /* Bug5: grab parent frame from cross-pattern handoff (set by caller) */\n"
        "        (*_zz_np)->_parent_frame = _pending_parent_frame;\n"
        "        _pending_parent_frame = -1;  /* consume */\n"
        "    }\n"
        "    %s *z = *_zz_np;\n"
        "    int64_t _cur_np = *_cur_ptr_np;\n",
        fnname, tyname, tyname, tyname);

    /* 3. Emit #defines so code body uses bare names via z-> */
    decl_emit_defines(out_file);

    /* 4. Entry dispatch */
    fprintf(out_file,
        "    if (_entry_np == 0) goto %s;\n"
        "    if (_entry_np == 1) goto %s;\n"
        "    goto %s;\n",
        root_α, root_β, ω_lbl);

    /* 5. Splice the code body */
    if (code_buf && code_size > 0)
        fwrite(code_buf, 1, code_size, out_file);
    free(code_buf);

    /* 6. Success/fail exits */
    fprintf(out_file,
        "    %s:;\n"
        "        *_cur_ptr_np = _cur_np;\n",
        γ_lbl);
    cond_emit_assigns(out_file, 0);   /* flush . captures at pattern γ */
    fprintf(out_file,
        "        return STRVAL(\"\");\n"
        "    %s:;\n"
        "        return FAILDESCR;\n",
        ω_lbl);

    /* 7. Emit #undefs and close function */
    decl_emit_undefs(out_file);
    fprintf(out_file, "}\n\n");
}

/* =======================================================================
 * byrd_emit_standalone — emit a complete standalone test program
 * matching the sprint oracle style.  Used by tests that compile
 * a .sno file with scrip-cc in byrd mode and want a standalone executable.
 * ======================================================================= */

static void emit_standalone(EXPR_t *pat, FILE *out_file,
                          const char *subject,
                          const char *root_name) {
    fprintf(out_file,
        "#include <stdint.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n"
        "#include \"../../src/runtime/runtime.h\"\n\n");

    fprintf(out_file, "int main(void) {\n");
    fprintf(out_file, "    const char *subject     = \"%s\";\n", subject);
    fprintf(out_file, "    int64_t     subject_len = %d;\n", (int)strlen(subject));
    fprintf(out_file, "    int64_t     cursor      = 0;\n");
    fprintf(out_file, "    (void)cursor; (void)subject; (void)subject_len;\n\n");

    emit_pattern(pat, out_file,
                      root_name,
                      "subject", "subject_len", "cursor",
                      "root_MATCH_SUCCESS",
                      "root_MATCH_FAIL");

    fprintf(out_file,
        "    root_MATCH_SUCCESS: return 0; /* matched */\n"
        "    root_MATCH_FAIL:    return 1; /* failed  */\n"
        "}\n");
}
static char *cs_alloc(const char *s) {
    char base[512]; int j=0;
    base[j++]='_';
    for (int i=0; s[i]&&j<510; i++) {
        unsigned char c=(unsigned char)s[i];
        base[j++]=(isalnum(c)||c=='_')?(char)c:'_';
    }
    base[j]='\0';
    return strdup(base);
}

/* Label registry — per-function, reset at the start of each function body.
 * Detects collisions and appends disambiguation suffix.
 * Maps original SNOBOL4 label → unique C label within the current function. */
#define LREG_MAX 8192
typedef struct { char *orig; char *csafe; } LReg;
static LReg  lreg[LREG_MAX];
static int   lreg_count = 0;

static void lreg_reset(void) {
    for (int i=0; i<lreg_count; i++) { free(lreg[i].orig); free(lreg[i].csafe); }
    lreg_count = 0;
}

/* cs_label: return unique C label for SNOBOL4 label s within current function */
static const char *cs_label(const char *s) {
    /* Return previously registered entry */
    for (int i=0; i<lreg_count; i++)
        if (strcmp(lreg[i].orig, s)==0) return lreg[i].csafe;
    /* Compute base C name */
    char *base = cs_alloc(s);
    /* Find unique candidate (no collision with existing csafe entries) */
    char candidate[520];
    strcpy(candidate, base);
    int suffix=2, collision=1;
    while (collision) {
        collision=0;
        for (int i=0; i<lreg_count; i++)
            if (strcmp(lreg[i].csafe, candidate)==0) { collision=1; break; }
        if (collision) snprintf(candidate, sizeof candidate, "%s_%d", base, suffix++);
    }
    free(base);
    if (lreg_count < LREG_MAX) {
        lreg[lreg_count].orig  = strdup(s);
        lreg[lreg_count].csafe = strdup(candidate);
        lreg_count++;
    }
    return lreg[lreg_count-1].csafe;
}

/* cs: C-safe name for variables (not labels) — simple, no registry */
/* ============================================================
 * Value expression emission → DESCR_t
 * ============================================================ */
static void emit_expr(EXPR_t *e);
static void emit_pat(EXPR_t *e);
int expr_contains_pattern(EXPR_t *e);

/* -----------------------------------------------------------------------
 * emit_chain_pretty — multi-line indented binary chain emitter
 *
 * Walks a left-associative binary chain (E_SEQ/E_CONCAT/E_ALT) collecting all
 * leaves, then emits as indented multi-line nested calls.
 *
 * fn_name:  "CONCAT_fn", "pat_cat", "pat_alt"
 * emit_leaf: emit_expr or emit_pat — called for each leaf node
 * min_depth: minimum chain depth before pretty-printing kicks in (3)
 *
 * Output form (depth 4, fn="CONCAT_fn"):
 *   CONCAT_fn(
 *       CONCAT_fn(
 *           CONCAT_fn(leaf0, leaf1),
 *           leaf2),
 *       leaf3)
 * ----------------------------------------------------------------------- */
#define CHAIN_MAX 64
static void emit_chain_pretty(EXPR_t *e, int kind,
                               const char *fn_name,
                               void (*emit_leaf)(EXPR_t *),
                               int min_depth) {
    /* With n-ary children[], the leaves are already flat: children[0..nchildren-1] */
    int depth = e->nchildren;

    if (depth < min_depth) {
        /* Short — keep inline */
        C("%s(", fn_name);
        for (int _ci = 0; _ci < e->nchildren; _ci++) {
            if (_ci > 0) C(",");
            emit_leaf(e->children[_ci]);
        }
        C(")");
        return;
    }

    /* Collect leaves from flat children array */
    EXPR_t *leaves[CHAIN_MAX];
    int n = e->nchildren < CHAIN_MAX ? e->nchildren : CHAIN_MAX;
    for (int _ci = 0; _ci < n; _ci++) leaves[_ci] = e->children[_ci];

    /* Emit: nested left-associative with 4-space indent per nesting level.
     *
     * For n leaves:
     *   fn(                  <- open (n-2) times
     *     fn(
     *       fn(leaf0, leaf1),
     *       leaf2),
     *     leaf3)
     */
    int indent = 4;
    /* Opening preamble: (n-2) lines each starting a new concat level */
    for (int i = 0; i < n - 2; i++) {
        C("%s(\n", fn_name);
        for (int s = 0; s < indent; s++) C(" ");
    }
    /* Innermost pair */
    C("%s(", fn_name);
    emit_leaf(leaves[0]);
    C(",\n");
    for (int s = 0; s < indent; s++) C(" ");
    emit_leaf(leaves[1]);
    C(")");
    /* Close each outer level, appending next right-arg */
    for (int i = 2; i < n; i++) {
        C(",\n");
        for (int s = 0; s < indent; s++) C(" ");
        emit_leaf(leaves[i]);
        C(")");
    }
}
#undef CHAIN_MAX

static void emit_expr(EXPR_t *e) {
    if (!e) { C("NULVCL"); return; }
    switch (e->kind) {
    case E_NUL:    C("NULVCL"); break;
    case E_QLIT:     C("STRVAL_fn("); escape_string(e->sval); C(")"); break;
    case E_ILIT:     C("INTVAL_fn(%ld)", e->ival); break;
    case E_FLIT:    C("real(%g)", e->dval); break;
    case E_VAR:
        if (is_io_name(e->sval)) C("NV_GET_fn(\"%s\")", e->sval);
        else C("get(%s)", cs(e->sval));
        break;
    case E_KW: C("kw(\"%s\")", e->sval); break;

    case E_INDR:
        if (!e->children[0] || (!e->children[1] && !(e->children[0]->kind == E_VAR || e->children[0]->kind == E_FNC))) {
            /* $expr — indirect lookup.
             * Old tree: left=NULL, right=operand.
             * New flat tree (M-FLAT-NARY): children[0]=operand (no right child).
             * Accept both: if children[0] is not VART/FNC it's a $-deref operand. */
            EXPR_t *operand = e->children[1] ? e->children[1] : e->children[0];
            C("deref("); emit_expr(operand); C(")");
        } else if (e->children[0]->kind == E_VAR) {
            /* *varname — deferred pattern reference (resolved at MATCH_fn time) */
            C("var_as_pattern(pat_ref(\"%s\"))", e->children[0]->sval);
        } else if (e->children[0]->kind == E_FNC && e->children[0]->nchildren >= 1
                   && !is_defined_function(e->children[0]->sval)) {

            /* *varname(arg...) — parser misparse: *varname concatenated with (arg).
             * SNOBOL4 continuation lines cause the parser to greedily consume the
             * next '(' as a function-call argument to varname.  The correct
             * semantics are: deferred-ref(*varname) cat arg. */
            C("CONCAT_fn(var_as_pattern(pat_ref(\"%s\")),", e->children[0]->sval);
            emit_expr(e->children[0]->children[0]);
            C(")");
        } else {
            /* *(expr) — deref of compound expression */
            C("deref("); emit_expr(e->children[0]); C(")");
        }
        break;

    case E_NEG: C("neg("); emit_expr(e->children[0]); C(")"); break;

    case E_CONCAT:
        emit_chain_pretty(e, E_CONCAT, "CONCAT_fn", emit_expr, 2);
        break;

    case E_OPSYN: C("APPLY_fn(\"reduce\",(DESCR_t[]){"); emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C("},2)"); break;
    case E_ADD:    C("add(");    emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C(")"); break;
    case E_SUB:    C("sub(");    emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C(")"); break;
    case E_MPY:    C("mul(");    emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C(")"); break;
    case E_DIV:    C("DIVIDE_fn(");    emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C(")"); break;
    case E_POW:    C("POWER_fn(");    emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C(")"); break;
    case E_ALT:
        /* Same: if either side is pattern-valued, use pat_alt */
        if (expr_contains_pattern(e->children[0]) || expr_contains_pattern(e->children[1])) {
            C("pat_alt("); emit_pat(e->children[0]); C(","); emit_pat(e->children[1]); C(")");
        } else {
            C("alt("); emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C(")");
        }
        break;

    /* capture nodes — in value context, evaluate the child */
    case E_CAPT_COND: emit_expr(e->children[0]); break;
    case E_CAPT_IMM:  emit_expr(e->children[0]); break;

    case E_FNC:
        if (e->nchildren == 0) {
            C("APPLY_fn(\"%s\",NULL,0)", e->sval);
        } else {
            C("APPLY_fn(\"%s\",(DESCR_t[]){", e->sval);
            for (int i=0; i<e->nchildren; i++) {
                if (i) { C(","); } emit_expr(e->children[i]);
            }
            C("},%d)", e->nchildren);
        }
        break;

    case E_IDX:
        /* E_IDX = canonical (was E_IDX via compat alias — dead C backend).
         * Named-array path: sval set. Postfix-subscript path: sval NULL. */
        if (e->sval) {
            C("aref(%s,(DESCR_t[]){", cs(e->sval));
            for (int i=0; i<e->nchildren; i++) {
                if (i) { C(","); } emit_expr(e->children[i]);
            }
            C("},%d)", e->nchildren);
        } else {
            C("INDEX_fn("); emit_expr(e->children[0]); C(",(DESCR_t[]){");
            for (int i=1; i<e->nchildren; i++) {
                if (i>1) { C(","); } emit_expr(e->children[i]);
            }
            C("},%d)", e->nchildren - 1);
        }
        break;

    case E_CAPT_CUR:
        /* @var — cursor position capture: evaluates to cursor int */
        C("cursor_get(\"%s\")", e->sval);
        break;

    case E_ASSIGN:
        /* var = expr inside expression context */
        C("assign_expr(%s,", cs(e->children[0]->sval)); emit_expr(e->children[1]); C(")");
        break;

    default:
        /* IR kinds not implemented in dead C backend — excluded from reorg */
        C("UNIMPL_KIND_%d", (int)e->kind);
        break;
    }
}

/* ============================================================
 * Pattern expression emission → PATND_t*
 *
 * Same EXPR_t nodes, different routing:
 *   E_FNC  → pat_builtin or pat_call
 *   E_SEQ  → pat_cat
 *   E_ALT   → pat_alt
 *   E_CAPT_COND  → pat_cond(child_pat, varname)
 *   E_CAPT_IMM   → pat_imm(child_pat, varname)
 *   E_INDR → pat_ref(varname)   (deferred pattern reference *X)
 *   E_QLIT   → pat_lit(STRVAL_fn)
 *   E_VAR   → pat_var(varname)   (pattern variable)
 * ============================================================ */
static void emit_pat(EXPR_t *e);

static void emit_pat(EXPR_t *e) {
    if (!e) { C("pat_epsilon()"); return; }
    switch (e->kind) {
    case E_QLIT:
        C("pat_lit("); escape_string(e->sval); C(")"); break;

    case E_VAR:
        C("pat_var(\"%s\")", e->sval); break;

    case E_INDR:
        /* *X — deferred pattern reference */
        if (e->children[0] && e->children[0]->kind == E_VAR)
            C("pat_ref(\"%s\")", e->children[0]->sval);
        else if (e->children[0] && e->children[0]->kind == E_FNC && e->children[0]->nchildren >= 1
                 && !is_defined_function(e->children[0]->sval)) {
            /* *varname(arg...) — continuation-line misparse: deref-ref cat arg
             * Only applies when varname is NOT a known function (it's a pat var). */
            C("pat_cat(pat_ref(\"%s\"),", e->children[0]->sval);
            emit_pat(e->children[0]->children[0]);
            C(")");
        } else {
            EXPR_t *op = e->children[1] ? e->children[1] : e->children[0];
            C("pat_deref("); emit_expr(op); C(")");
        }
        break;

    case E_SEQ:
        emit_chain_pretty(e, E_SEQ, "pat_cat", emit_pat, 2);
        break;

    case E_MPY:
        /* pat * x — parsed as arithmetic multiply, but in pattern context
         * this is: left_pattern CONCAT_fn *right (deferred ref to right) */
        C("pat_cat("); emit_pat(e->children[0]); C(",");
        if (e->children[1] && e->children[1]->kind == E_VAR)
            C("pat_ref(\"%s\")", e->children[1]->sval);
        else { C("pat_deref("); emit_expr(e->children[1]); C(")"); }
        C(")"); break;

    case E_OPSYN:
        /* & in pattern context: reduce(left, right) — must fire at MATCH TIME.
         * reduce() calls EVAL("epsilon . *Reduce(t, n)") where n may contain
         * nTop() — which must be evaluated at MATCH_fn time, not build time.
         * Use pat_user_call to defer the call until the engine executes
         * this node during pattern matching. */
        C("pat_user_call(\"reduce\",(DESCR_t[]){"); emit_expr(e->children[0]); C(","); emit_expr(e->children[1]); C("},2)"); break;

    case E_ALT:
        emit_chain_pretty(e, E_ALT, "pat_alt", emit_pat, 2);
        break;

    case E_CAPT_COND: {
        /* pat . var */
        const char *varname = (e->children[1] && e->children[1]->kind==E_VAR)
                              ? e->children[1]->sval : "?";
        C("pat_cond("); emit_pat(e->children[0]); C(",\"%s\")", varname); break;
    }
    case E_CAPT_IMM: {
        /* pat $ var */
        const char *varname = (e->children[1] && e->children[1]->kind==E_VAR)
                              ? e->children[1]->sval : "?";
        C("pat_imm("); emit_pat(e->children[0]); C(",\"%s\")", varname); break;
    }

    case E_FNC: {
        /* Route known builtins to pat_* */
        const char *n = e->sval;
        /* B0: zero-arg pattern; B1i: one int64_t arg; B1s: one string arg; B1v: one DESCR_t arg */
        #define B0(nm,fn)  if(strcasecmp(n,nm)==0){C(fn"()");break;}
        #define B1i(nm,fn) if(strcasecmp(n,nm)==0&&e->nchildren>=1){C(fn"(to_int(");emit_expr(e->children[0]);C("))");break;}
        #define B1s(nm,fn) if(strcasecmp(n,nm)==0&&e->nchildren>=1){C(fn"(VARVAL_fn(");emit_expr(e->children[0]);C("))");break;}
        #define B1v(nm,fn) if(strcasecmp(n,nm)==0&&e->nchildren>=1){C(fn"(");emit_expr(e->children[0]);C(")");break;}
        B0("ARB","pat_arb")  B0("REM","pat_rem")
        B0("DT_FAIL","pat_fail") B0("ABORT","pat_abort")
        /* FENCE() = bare fence; FENCE(p) = fence with sub-pattern */
        if(strcasecmp(n,"FENCE")==0){
            if(e->nchildren>=1){C("pat_fence_p(");emit_pat(e->children[0]);C(")");}
            else{C("pat_fence()");}
            break;
        }
        B0("SUCCEED","pat_succeed")
        B0("BAL","pat_bal")
        B1i("LEN","pat_len")   B1i("POS","pat_pos")
        B1i("RPOS","pat_rpos") B1i("TAB","pat_tab")
        B1i("RTAB","pat_rtab")
        B1s("SPAN","pat_span") B1s("BREAK","pat_break")
        B1s("NOTANY","pat_notany") B1s("ANY","pat_any")
        B1v("ARBNO","pat_arbno")
        #undef B0
        #undef B1i
        #undef B1s
        #undef B1v
        /* user-defined pattern function — use pat_user_call so the function
         * fires at MATCH TIME (XATP materialisation), not at build time.
         * This correctly handles nPush()/nPop() side effects per MATCH_fn attempt,
         * and reduce()/shift() which return pattern objects at materialisation time.
         *
         * EXCEPTION: if name is NOT a known/defined function AND has args, the
         * parser has misinterpreted  var(pat)  as a function call.  In SNOBOL4,
         * a variable followed by a parenthesised expression is CONCATENATION, not
         * a call.  Emit: pat_cat(pat_var(n), emit_pat(args[0])) instead. */
        if (e->nchildren > 0 && !is_defined_function(n)) {
            /* variable(args) → CONCAT(var, grouped_pat) */
            C("pat_cat(pat_var(\"%s\"),", n);
            if (e->nchildren == 1) {
                emit_pat(e->children[0]);
            } else {
                /* Multiple args: emit as successive concatenations */
                for (int i = 0; i < e->nchildren; i++) {
                    if (i < e->nchildren - 1) C("pat_cat(");
                }
                emit_pat(e->children[0]);
                for (int i = 1; i < e->nchildren; i++) { C(","); emit_pat(e->children[i]); C(")"); }
            }
            C(")");
            break;
        }
        if (e->nchildren == 0) {
            C("pat_user_call(\"%s\",NULL,0)", n);
        } else {
            /* Pattern-constructor functions are called eagerly at BUILD TIME.
             * They return a DESCR_t of type PATTERN — wrap with var_as_pattern.
             * reduce(t,n), shift(p,t), EVAL(s) → build-time call → APPLY_fn.
             * Side-effect functions (nPush, nPop, nInc, Reduce, Shift, TZ, etc.)
             * must fire at MATCH TIME → stay as pat_user_call. */
            static const char *_build_time_fns[] = {
                "reduce", "shift", "EVAL", NULL
            };
            int _is_build = 0;
            for (int _ci = 0; _build_time_fns[_ci]; _ci++) {
                if (strcasecmp(n, _build_time_fns[_ci]) == 0) { _is_build = 1; break; }
            }
            if (_is_build) {
                C("var_as_pattern(APPLY_fn(\"%s\",(DESCR_t[]){", n);
                for (int i=0; i<e->nchildren; i++) { if(i) C(","); emit_expr(e->children[i]); }
                C("},%d))", e->nchildren);
            } else {
                C("pat_user_call(\"%s\",(DESCR_t[]){", n);
                for (int i=0; i<e->nchildren; i++) { if(i) C(","); emit_expr(e->children[i]); }
                C("},%d)", e->nchildren);
            }
        }
        break;
    }

    /* value nodes that shouldn't appear in pattern context — treat as var */
    case E_IDX:
    case E_CAPT_CUR:
    case E_ASSIGN:
    default:
        C("pat_val("); emit_expr(e); C(")"); break;
    }
}

/* ============================================================
 * Emit lvalue assignment target
 * ============================================================ */

/* cur_fn_def and is_fn_local() are defined after FnDef (below) */
static int is_fn_local(const char *varname);

static void emit_assign_target(EXPR_t *lhs, const char *rhs_str) {
    if (!lhs) return;
    if (lhs->kind == E_VAR) {
        C("set(%s, %s);\n", cs(lhs->sval), rhs_str);
        C("NV_SET_fn(\"%s\", %s);\n", lhs->sval, cs(lhs->sval));
    } else if (lhs->kind == E_IDX) {
        /* E_IDX = canonical (absorbs E_IDX).
         * Named array (sval set): aset(name, subs, val)
         * Postfix subscript (sval NULL): aset(expr, subs, val) */
        if (lhs->sval) {
            C("aset(%s,(DESCR_t[]){", cs(lhs->sval));
            for (int i=0; i<lhs->nchildren; i++) {
                if (i) C(",");
                PP_EXPR(lhs->children[i], 0);
            }
            C("},%d,%s);\n", lhs->nchildren, rhs_str);
        } else {
            C("aset("); PP_EXPR(lhs->children[0], 0);
            C(",(DESCR_t[]){");
            for (int i=1; i<lhs->nchildren; i++) {
                if (i>1) C(",");
                PP_EXPR(lhs->children[i], 0);
            }
            C("},%d,%s);\n", lhs->nchildren - 1, rhs_str);
        }
    } else if (lhs->kind == E_KW) {
        C("kw_set(\"%s\",%s);\n", lhs->sval, rhs_str);
    } else if (lhs->kind == E_INDR) {
        /* $X = val: flat tree children[0]=operand */
        C("iset("); PP_EXPR(lhs->children[0], 0); C(",%s);\n", rhs_str);
    } else if (lhs->kind == E_FNC && lhs->nchildren == 1) {
        /* field accessor lvalue: val(n) = x  ->  FIELD_SET_fn(n, "val", x) */
        C("FIELD_SET_fn("); PP_EXPR(lhs->children[0], 0);
        C(", \"%s\", %s);\n", lhs->sval, rhs_str);
    } else {
        /* complex lvalue: evaluate and assign indirectly */
        C("iset("); PP_EXPR(lhs, 0); C(",%s);\n", rhs_str);
    }
}

/* Current function being emitted (NULL = main) */
static const char *cur_fn_name = NULL;

/* Return 1 if label is defined within the body region of fn_name.
 * This is used to detect cross-function goto references. */
static int label_is_in_fn_body(const char *label, const char *fn_name);
static void emit_computed_goto_inline(const char *label, const char *fn);
static int is_body_boundary(const char *label, const char *cur_fn);

/* Forward: current stmt next uid for fallthrough */
/* (cur_stmt_next_uid already declared above) */

/* ============================================================
 * Emit goto field
 * ============================================================ */

/* Emit a single branch target — handles RETURN/FRETURN/NRETURN/END specially.
 * Also handles:
 *   "error"     -> FRETURN (beauty.sno idiom: :F(error) means fail the function)
 *   "_COMPUTED" -> computed-goto stub (just fall through)
 *   Cross-fn labels -> fallthrough (label is in a different function's body)
 * All SNOBOL4 label -> C label mappings go through cs_label() for uniqueness.
 */

/* Capture emit_goto_target output as a string (for use with PS/PL macros).
 * PG/PS macros expect just the goto *target* (without "goto " prefix),
 * because pretty_line col3 adds "goto " itself.
 * For "return ..." targets (trampoline mode), returns the full statement
 * with a leading "!" marker so callers can detect and use C() instead.
 * Returns heap-allocated string; caller must free(). */
static void emit_goto_target(const char *label, const char *fn);
static char *goto_target_str(const char *label, const char *fn) {
    char *buf = NULL; size_t sz = 0;
    FILE *tmp = open_memstream(&buf, &sz);
    FILE *saved = out; out = tmp;
    emit_goto_target(label, fn);
    out = saved; fclose(tmp);
    /* Strip leading "goto " — pretty_line adds it back in col3 */
    if (buf && strncmp(buf, "goto ", 5) == 0) {
        char *stripped = strdup(buf + 5);
        free(buf);
        return stripped;
    }
    /* Return statements or other non-goto fragments: return as-is.
     * Callers must use C("%s;\n", tgt) not PG(tgt) for these. */
    return buf; /* caller frees */
}

/* Emit a conditional or unconditional goto line in 3-column format.
 * cond: NULL/"" for unconditional; "if(_ok)" etc for conditional.
 * tgt: raw output of goto_target_str — either a label (use PG/PS)
 *      or a full "return ..." statement (use E). */
static void emit_pretty_goto(const char *tgt, const char *cond) {
    if (!tgt || !tgt[0]) return;
    int is_return   = (strncmp(tgt, "return", 6) == 0);
    int is_computed = (tgt[0] == '{');   /* computed-goto inline block */
    if (is_return || is_computed) {
        /* Can't put return/block in col3 — use C() raw */
        if (cond && cond[0]) C("    %s { %s; }\n", cond, tgt);
        else                  C("    %s;\n", tgt);
    } else {
        if (cond && cond[0]) PS(tgt, "%s", cond);
        else                  PG(tgt);
    }
}

static void emit_goto_target(const char *label, const char *fn) {
    int in_main = !fn || strcasecmp(fn, "main") == 0;

    /* ---- Trampoline mode: every exit is a return, not a goto ---- */
    if (trampoline_mode) {
        if      (strcasecmp(label,"RETURN") ==0 ||
                 strcasecmp(label,"NRETURN")==0) {
            if (in_main) { C("return NULL"); return; }
            C("return _tramp_return_%s", fn); return;
        }
        else if (strcasecmp(label,"FRETURN")==0 ||
                 strcasecmp(label,"error")  ==0) {
            if (in_main) { C("return NULL"); return; }
            C("return _tramp_freturn_%s", fn); return;
        }
        else if (strcasecmp(label,"END")==0) {
            C("return NULL"); return;
        }
        else if (strncasecmp(label,"$COMPUTED",9)==0) {
            /* Computed goto: $COMPUTED:expr_text
             * Re-parse the stored expression, emit runtime label lookup. */
            const char *expr_src = (label[9]==':') ? label+10 : NULL;
            if (expr_src && *expr_src) {
                EXPR_t *ce = parse_expr_from_str(expr_src);
                if (ce) {
                    C("{ const char *_cgoto_lbl = VARVAL_fn(");
                    emit_expr(ce);
                    C("); return sno_computed_goto(_cgoto_lbl); }");
                } else {
                    C("return (void*)_tramp_next_%d", cur_stmt_next_uid);
                }
            } else {
                C("return (void*)_tramp_next_%d", cur_stmt_next_uid);
            }
            return;
        }
        /* Cross-scope: fall through */
        if (label_is_in_fn_body(label, NULL) && !label_is_in_fn_body(label, fn)) {
            C("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        if (!in_main && !label_is_in_fn_body(label, fn)) {
            C("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        C("return (void*)block%s", cs_label(label));
        return;
    }

    /* ---- Classic goto mode (unchanged) ---- */
    if      (strcasecmp(label,"RETURN") ==0) {
        if (in_main) { C("goto _SNO_END"); return; }
        C("goto _SNO_RETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"FRETURN")==0) {
        if (in_main) { C("goto _SNO_END"); return; }
        C("goto _SNO_FRETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"NRETURN")==0) {
        if (in_main) { C("goto _SNO_END"); return; }
        C("goto _SNO_RETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"END")    ==0) {
        if (!in_main) { C("goto _SNO_FRETURN_%s", fn); return; }
        C("goto _SNO_END"); return;
    }
    else if (strcasecmp(label,"error")  ==0) {
        if (in_main) { C("goto _SNO_END"); return; }
        C("goto _SNO_FRETURN_%s", fn); return;
    }
    else if (strncasecmp(label,"$COMPUTED",9)==0 || strcasecmp(label,"_COMPUTED")==0) {
        /* Computed goto: delegate to helper defined after fn_table. */
        emit_computed_goto_inline(label, fn);
        return;
    }
    if (label_is_in_fn_body(label, NULL) && !label_is_in_fn_body(label, fn)) {
        C("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    if (!in_main && !label_is_in_fn_body(label, fn)) {
        C("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    C("goto _L%s", cs_label(label));
}

static void emit_goto(SnoGoto *g, const char *fn, int result_ok) {
    if (trampoline_mode) {
        if (!g) { C("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid); return; }
        if (g->uncond) {
            C("    "); emit_goto_target(g->uncond, fn); C(";\n");
        } else {
            if (result_ok) {
                if (g->onsuccess) { C("    if(_ok) { "); emit_goto_target(g->onsuccess, fn); C("; }\n"); }
                if (g->onfailure) { C("    if(!_ok) { "); emit_goto_target(g->onfailure, fn); C("; }\n"); }
            } else {
                if (g->onsuccess) { C("    "); emit_goto_target(g->onsuccess, fn); C(";\n"); }
            }
            C("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
        }
        return;
    }
    if (!g) { char _nl[64]; snprintf(_nl,sizeof _nl,"_SNO_NEXT_%d",cur_stmt_next_uid); PG(_nl); return; }
    if (g->uncond) {
        char *tgt = goto_target_str(g->uncond, fn);
        emit_pretty_goto(tgt, NULL); free(tgt);
    } else {
        if (result_ok) {
            if (g->onsuccess) { char *tgt=goto_target_str(g->onsuccess,fn); emit_pretty_goto(tgt,"if(_ok)"); free(tgt); }
            if (g->onfailure) { char *tgt=goto_target_str(g->onfailure,fn); emit_pretty_goto(tgt,"if(!_ok)"); free(tgt); }
        } else {
            if (g->onsuccess) { char *tgt=goto_target_str(g->onsuccess,fn); emit_pretty_goto(tgt,NULL); free(tgt); }
            if (g->onfailure) { /* can't reach failure — skip */ }
        }
        char _nl[64]; snprintf(_nl,sizeof _nl,"_SNO_NEXT_%d",cur_stmt_next_uid); PG(_nl);
    }
}

/* ============================================================
 * Post-parse pattern-statement repair
 *
 * The grammar is LALR(1) and cannot always distinguish:
 *   subject pattern = replacement    (pattern MATCH_fn)
 *   subject_expr = replacement       (pure assignment)
 * when pattern primitives (LEN, POS, etc.) appear inside the subject_expr.
 * The lexer returns PAT_BUILTIN only at bstack_top==0, but PAT_BUILTIN IS
 * also in the `primary` grammar rule for value exprs, causing the parser to
 * absorb the pattern into the subject.
 *
 * This function detects the case and repairs the STMT_t in place.
 * It looks for: s->pattern==NULL, s->replacement==E_NUL, and the subject
 * tree contains a PAT_BUILTIN call in a position that looks like a pattern start.
 * ============================================================ */

static int is_pat_builtin_call(EXPR_t *e) {
    if (!e || e->kind != E_FNC) return 0;
    static const char *pb[] = {
        "LEN","POS","RPOS","TAB","RTAB","SPAN","BREAK",
        "NOTANY","ANY","ARB","REM","DT_FAIL","ABORT",
        "FENCE","SUCCEED","BAL","ARBNO", NULL
    };
    for (int i = 0; pb[i]; i++)
        if (strcasecmp(e->sval, pb[i]) == 0) return 1;
    return 0;
}

/* Returns 1 if expr e is a pattern node (E_FNC to pat_builtin, E_CAPT_COND capture,
 * E_ALT, or E_SEQ whose left child is a pattern). */
static int is_pat_node(EXPR_t *e) {
    if (!e) return 0;
    if (is_pat_builtin_call(e)) return 1;
    if (e->kind == E_CAPT_COND)   return 1;  /* .var capture */
    if (e->kind == E_ALT)    return 1;  /* | alternation */
    if (e->kind == E_OPSYN) return 1;  /* & reduce() call — always pattern context */
    /* E_MPY(pat_node, x) — parsed from "pat *x" where * is multiplication token
     * but semantically is pattern-CONCAT_fn with deferred ref *x */
    if (e->kind == E_MPY && is_pat_node(e->children[0])) return 1;
    return 0;
}

/* Recursively checks if any node in e's subtree indicates pattern context.
 * Used to decide whether a pure assignment RHS should use emit_pat.
 * Indicators: E_INDR (*var — always a pattern ref), E_OPSYN (& — reduce()),
 * E_CAPT_COND (. capture), E_ALT (| alternation in pattern context), E_FNC to
 * any pattern builtin including ARBNO/FENCE/etc. */
/* Returns 1 if the expression subtree rooted at e contains ANY pattern-valued
 * node.  Used by emit_expr to decide whether E_SEQ / E_ALT should be routed
 * through emit_pat (pat_cat / pat_alt) instead of the string path
 * (CONCAT_fn / alt).
 *
 * Key cases that are pattern-valued but NOT caught by is_pat_node:
 *   - E_INDR whose left child is E_VAR — "*varname" deferred pattern ref
 *   - E_SEQ or E_ALT whose subtree contains any of the above
 */
int expr_contains_pattern(EXPR_t *e) {
    if (!e) return 0;
    if (is_pat_node(e)) return 1;
    /* *varname — deferred pattern ref (grammar: left=NULL, right=E_VAR) */
    if (e->kind == E_INDR && e->children[1] && e->children[1]->kind == E_VAR) return 1;
    if (e->kind == E_INDR && e->children[0]  && e->children[0]->kind  == E_VAR) return 1;
    /* *varname(arg) — parser misparse deref+CONCAT_fn */
    if (e->kind == E_INDR && e->children[0] && e->children[0]->kind == E_FNC) return 1;
    /* recurse into children */
    if (e->kind == E_SEQ || e->kind == E_CONCAT || e->kind == E_ALT || e->kind == E_MPY)
        return expr_contains_pattern(e->children[0]) || expr_contains_pattern(e->children[1]);
    /* $ and . operators — pattern may be on the left side */
    if (e->kind == E_CAPT_IMM || e->kind == E_CAPT_COND)
        return expr_contains_pattern(e->children[0]);
    if (e->kind == E_FNC) {
        /* ARBNO, FENCE, etc. already caught by is_pat_builtin_call above.
         * Also treat reduce/EVAL_fn calls as pattern-valued when inside CONCAT_fn. */
        if (e->sval && (strcasecmp(e->sval,"reduce")==0 || strcasecmp(e->sval,"EVAL_fn")==0))
            return 1;
        for (int i = 0; i < e->nchildren; i++)
            if (expr_contains_pattern(e->children[i])) return 1;
    }
    return 0;
}

/* Walk the E_SEQ/E_CONCAT left spine. When we find a right child that is_pat_node,
 * detach it and everything after it into the pattern.
 * Returns the extracted pattern root, or NULL if nothing found.
 * *subj_out is set to the remaining subject (may be the original expr if
 * no split needed).
 *
 * The tree is LEFT-ASSOCIATIVE CONCAT_fn:
 *   (((STRVAL_fn, POS(0)), ANY('abc')), E_CAPT_COND(letter))
 * We walk the left spine, looking for the first right child that is a pat node.
 * When found at depth D, the pattern is: right_at_D CONCAT_fn right_at_D-1 CONCAT_fn ... CONCAT_fn right_at_0
 * assembled left-to-right.
 */
typedef struct { EXPR_t *subj; EXPR_t *pat; } SplitResult;

static EXPR_t *make_concat(EXPR_t *left, EXPR_t *right) {
    if (!left)  return right;
    if (!right) return left;
    EXPR_t *e = expr_new(E_CONCAT);
    e->children[0] = left; e->children[1] = right;
    return e;
}

static SplitResult split_spine(EXPR_t *e) {
    /* Null or non-CONCAT_fn node that's a pure value: subject only */
    if (!e) { SplitResult r = {NULL, NULL}; return r; }

    if (e->kind != E_CONCAT) {
        if (is_pat_node(e)) {
            SplitResult r = {NULL, e}; return r;   /* entire node is pattern */
        } else {
            SplitResult r = {e, NULL}; return r;   /* entire node is subject */
        }
    }

    /* e = E_CONCAT(left, right) */
    /* First check if RIGHT is a pattern node (first pat on the right spine) */
    if (is_pat_node(e->children[1])) {
        /* Split here: left is subject, right and above become pattern */
        SplitResult inner = split_spine(e->children[0]);
        /* inner.pat (if any) should be prepended, but since left was already
         * recursed and left's right IS the current pat... */
        /* Actually: the split is between left and right.
         * Subject = inner.subj (what was pure subject in e->children[0])
         * Pattern = inner.pat (any pattern found in e->children[0]'s right chain) CONCAT_fn e->children[1] */
        SplitResult r;
        r.subj = inner.subj;
        r.pat  = make_concat(inner.pat, e->children[1]);
        return r;
    }

    /* Right is not a pattern node. Recurse left. */
    SplitResult inner = split_spine(e->children[0]);
    if (!inner.pat) {
        /* No split found in left, and right is not a pattern. No split. */
        SplitResult r = {e, NULL}; return r;
    }
    /* Split found in left spine: reassemble */
    /* inner.subj is the new left, e->children[1] gets appended to the pattern */
    SplitResult r;
    r.subj = inner.subj;
    r.pat  = make_concat(inner.pat, e->children[1]);
    return r;
}

static EXPR_t *split_subject_pattern(EXPR_t *e, EXPR_t **subj_out) {
    if (!e) { *subj_out = NULL; return NULL; }
    SplitResult r = split_spine(e);
    *subj_out = r.subj;
    return r.pat;
}

/* Repair a misparsed pattern-MATCH_fn stmt.
 * Called when s->pattern==NULL and s->replacement is E_NUL (bare '=').
 * Also repairs pattern-MATCH_fn stmts with no replacement (s->replacement==NULL)
 * where the subject absorbed the pattern (no '=' present).
 * Returns 1 if the stmt was repaired. */
static int maybe_fix_pattern_stmt(STMT_t *s) {
    if (!s->subject) return 0;  /* no subject */
    /* Heuristic: if replacement is non-null non-E_NUL, this is a plain assignment,
     * not a pattern MATCH_fn. Skip. */
    if (s->replacement && s->replacement->kind != E_NUL) return 0;

    EXPR_t *new_subj = NULL;
    EXPR_t *new_pat  = split_subject_pattern(s->subject, &new_subj);

    if (!new_pat && !s->pattern) return 0;  /* nothing to fix */

    if (new_pat) {
        s->subject = new_subj;
        if (s->pattern) {
            /* Pattern already parsed (e.g. RPOS(0) at end). Prepend extracted
             * pattern from subject in front of the existing s->pattern. */
            s->pattern = make_concat(new_pat, s->pattern);
        } else {
            s->pattern = new_pat;
        }
        return 1;
    }
    return 0;
}

/* ============================================================
 * pat_is_anchored — returns 1 if the leftmost node of pattern e
 * is POS(), meaning the pattern anchors at a specific position.
 * Used to decide whether to wrap in ARB for substring scan.
 * ============================================================ */
static int pat_is_anchored(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC && e->sval && strcasecmp(e->sval, "POS") == 0) {
        /* Only POS(0) literal anchors at start — no ARB needed.
         * Dynamic POS(N) needs ARB scan to advance cursor to position N. */
        if (e->nchildren >= 1 && e->children[0]->kind == E_ILIT && e->children[0]->ival == 0)
            return 1;
        return 0;
    }
    if (e->kind == E_SEQ) return pat_is_anchored(e->children[0]);
    return 0;
}

/* ============================================================
 * emit_ok_goto — emit 3-column conditional :S/:F goto lines
 *
 * Replaces the repeated pattern:
 *   if (!s->go) { fallthrough; }
 *   else if (uncond) { emit_goto(g,fn,0); }
 *   else {
 *       if (onsuccess) PS(tgt, "if(_ok%d)", u);
 *       if (onfailure) PS(tgt, "if(!_ok%d)", u);
 *       fallthrough;
 *   }
 * Used by all three emit_stmt paths (pure-assign, pattern-match, expr-only).
 * ============================================================ */
static void emit_ok_goto(SnoGoto *g, const char *fn, int u) {
    char next_lbl[64];
    if (trampoline_mode)
        snprintf(next_lbl, sizeof next_lbl, "return (void*)_tramp_next_%d", cur_stmt_next_uid);
    else
        snprintf(next_lbl, sizeof next_lbl, "_SNO_NEXT_%d", cur_stmt_next_uid);

    if (!g) {
        emit_pretty_goto(next_lbl, NULL);
        return;
    }
    if (g->uncond) { emit_goto(g, fn, 0); return; }

    if (g->onsuccess) {
        char *tgt = goto_target_str(g->onsuccess, fn);
        char cond[32]; snprintf(cond, sizeof cond, "if(_ok%d)", u);
        emit_pretty_goto(tgt, cond);
        free(tgt);
    }
    if (g->onfailure) {
        char *tgt = goto_target_str(g->onfailure, fn);
        char cond[32]; snprintf(cond, sizeof cond, "if(!_ok%d)", u);
        emit_pretty_goto(tgt, cond);
        free(tgt);
    }
    emit_pretty_goto(next_lbl, NULL);
}

/* ============================================================
 * Emit one statement
 * ============================================================ */
static void emit_stmt(STMT_t *s, const char *fn) {
    /* Repair misparsed pattern-MATCH_fn stmts (grammar absorbs pattern into subject) */
    maybe_fix_pattern_stmt(s);

    C("/* line %d */\n", s->lineno);
    if (s->label) { char _sl[128]; snprintf(_sl, sizeof _sl, "_L%s", cs_label(s->label)); PLG(_sl, ""); }
    PS("", "trampoline_stno(%d);", s->lineno);

    /* label-only statement */
    if (!s->subject) {
        emit_goto(s->go, fn, 0);
        return;
    }

    /* ---- pure assignment: subject = replacement, no pattern ---- */
    if (!s->pattern && s->replacement) {
        int u=next_uid();
        /* If the RHS contains deferred refs (*var), reduce() calls (&), or
         * pattern builtins (ARBNO/FENCE/etc.), emit in pattern context so
         * E_SEQ becomes pat_cat and *var becomes pat_ref.
         * This handles: snoParse = nPush() ARBNO(*snoCommand) ... nPop() */
        if (expr_contains_pattern(s->replacement)) {
            int _col = fprintf(out, "DESCR_t _v%d = ", u);
            PP_PAT(s->replacement, _col); C(";\n");
        } else {
            int _col = fprintf(out, "DESCR_t _v%d = ", u);
            PP_EXPR(s->replacement, _col); C(";\n");
        }
        C("int _ok%d = !IS_FAIL_fn(_v%d);\n", u, u);
        C("if(_ok%d) {\n", u);
        char rhs[32]; snprintf(rhs,sizeof rhs,"_v%d",u);
        emit_assign_target_io(s->subject, rhs);
        C("}\n");
        /* emit goto using _ok%d for conditional :S/:F branches */
        emit_ok_goto(s->go, fn, u);
        return;
    }

    /* ---- null assign: subject = (empty RHS) — clears variable to null ---- */
    if (!s->pattern && !s->replacement && s->has_eq && s->subject) {
        C("{ /* null assign */\n");
        emit_assign_target_io(s->subject, "NULVCL");
        C("}\n");
        emit_ok_goto(s->go, fn, -1);
        return;
    }

    /* ---- pattern MATCH_fn: subject pattern [= replacement] ---- */
    /* Compiled Byrd box path — replaces pat_* / engine.c stopgap. */
    if (s->pattern) {
        int u = next_uid();
        C("/* byrd MATCH_fn u%d */\n", u);
        { int _col = fprintf(out, "DESCR_t _s%d = ", u); PP_EXPR(s->subject, _col); C(";\n"); }
        C("const char *_subj%d = VARVAL_fn(_s%d);\n", u, u);
        C("int64_t _slen%d = _subj%d ? (int64_t)strlen(_subj%d) : 0;\n", u, u, u);
        C("int64_t _cur%d  = 0;\n", u);
        C("int64_t _mstart%d = 0;\n", u);  /* cursor before MATCH_fn — for replacement */

        char root_lbl[64], ok_lbl[64], fail_lbl[64], done_lbl[64];
        snprintf(root_lbl, sizeof root_lbl, "_byrd_%d",      u);
        snprintf(ok_lbl,   sizeof ok_lbl,   "_byrd_%d_ok",   u);
        snprintf(fail_lbl, sizeof fail_lbl, "_byrd_%d_fail", u);
        snprintf(done_lbl, sizeof done_lbl, "_byrd_%d_done", u);

        char sv[32], sl[32], cv[32];
        snprintf(sv, sizeof sv, "_subj%d", u);
        snprintf(sl, sizeof sl, "_slen%d", u);
        snprintf(cv, sizeof cv, "_cur%d",  u);

        /* Declare _ok before the Byrd block (C: no jumps over declarations) */
        C("int _ok%d = 0;\n", u);
        /* NOTE: _mstart is NOT set here — it is set by SNO_MSTART node after ARB scans */
        /* Checkpoint $'@S' (tree stack) before the pattern match.
         * On match failure, restore to undo any Shift/Reduce side-effects
         * (e.g. a zero-match ARBNO leaves a stray Parse tree on @S). */
        C("DESCR_t _stk_save_%d = NV_GET_fn(\"@S\");\n", u);

        /* SNOBOL4 pattern matching is a substring scan: wrap pattern in ARB
         * unless the leftmost node is POS() which anchors to a position. */
        EXPR_t *scan_pat = s->pattern;
        if (!pat_is_anchored(s->pattern)) {
            EXPR_t *arb = expr_new(E_FNC);
            arb->sval = strdup("ARB");

            /* SNO_MSTART: zero-width node that captures cursor into _mstart after ARB scans */
            EXPR_t *mstart_node = expr_new(E_FNC);
            mstart_node->sval = strdup("SNO_MSTART");
            mstart_node->ival = u;  /* thread the statement uid so emit_byrd can name the var */

            EXPR_t *seq1 = expr_binary(E_SEQ, arb, mstart_node);

            EXPR_t *seq = expr_binary(E_SEQ, seq1, s->pattern);
            scan_pat = seq;
        }
        cond_reset();
        emit_pattern(scan_pat, out, root_lbl, sv, sl, cv, ok_lbl, fail_lbl);

        /* γ: MATCH_fn succeeded */
        PLG(ok_lbl, "");
        PS("", "_ok%d = 1;", u);
        cond_emit_assigns(out, u);   /* flush pending . captures */
        if (s->replacement || s->has_eq) {
            /* Replace matched region [_mstart%d .. _cur%d) with replacement.
             * If has_eq but replacement==NULL, that is a null replacement — delete match. */
            C("{\n");
            if (s->replacement) {
                C("    DESCR_t _r%d = ", u); PP_EXPR(s->replacement, 4 + 10 + 3); C(";\n");
            } else {
                C("    DESCR_t _r%d = STRVAL_fn(\"\");\n", u);  /* null replacement = empty */
            }
            C("    const char *_rs%d = VARVAL_fn(_r%d);\n", u, u);
            C("    int64_t _rlen%d = _rs%d ? (int64_t)strlen(_rs%d) : 0;\n", u, u, u);
            C("    int64_t _tail%d = _slen%d - _cur%d;\n", u, u, u);
            C("    int64_t _newlen%d = _mstart%d + _rlen%d + _tail%d;\n", u, u, u, u);
            C("    char *_nb%d = (char*)GC_malloc(_newlen%d + 1);\n", u, u);
            C("    if (_mstart%d > 0) memcpy(_nb%d, _subj%d, (size_t)_mstart%d);\n", u, u, u, u);
            C("    if (_rlen%d  > 0) memcpy(_nb%d + _mstart%d, _rs%d, (size_t)_rlen%d);\n", u, u, u, u, u);
            C("    if (_tail%d  > 0) memcpy(_nb%d + _mstart%d + _rlen%d, _subj%d + _cur%d, (size_t)_tail%d);\n", u, u, u, u, u, u, u);
            C("    _nb%d[_newlen%d] = '\\0';\n", u, u);
            C("    _s%d = STRVAL(_nb%d);\n", u, u);
            /* write back to subject variable */
            if (s->subject && s->subject->kind == E_VAR) {
                if (is_io_name(s->subject->sval))
                    C("    NV_SET_fn(\"%s\", _s%d);\n", s->subject->sval, u);
                else {
                    C("    set(%s, _s%d);\n", cs(s->subject->sval), u);
                    C("    NV_SET_fn(\"%s\", %s);\n", s->subject->sval, cs(s->subject->sval));
                }
            }
            C("}\n");
        }
        PG(done_lbl);

        /* ω: MATCH_fn failed — restore @S to pre-match state */
        PLG(fail_lbl, "");
        PS("", "NV_SET_fn(\"@S\", _stk_save_%d);", u);
        PS("", "_ok%d = 0;", u);

        PLG(done_lbl, "");

        /* emit goto using _ok%d */
        emit_ok_goto(s->go, fn, u);
        return;
    }

    /* ---- expression evaluation only ---- */
    {
        int u=next_uid();
        { int _col = fprintf(out, "DESCR_t _v%d = ", u); PP_EXPR(s->subject, _col); C(";\n"); }
        C("int _ok%d = !IS_FAIL_fn(_v%d);\n", u, u);
        /* emit goto using _ok%d */
        emit_ok_goto(s->go, fn, u);
    }
}

/* ============================================================
 * IO assignment routing
 * ============================================================ */

static void emit_assign_target_io(EXPR_t *lhs, const char *rhs_str) {
    if (lhs && lhs->kind == E_VAR && is_io_name(lhs->sval)) {
        C("NV_SET_fn(\"%s\", %s);\n", lhs->sval, rhs_str);
        return;
    }
    emit_assign_target(lhs, rhs_str);
}

/* ============================================================
 * DEFINE_fn function table
 *
 * Pre-pass: scan for DEFINE_fn('fn(a,b)loc1,loc2') calls.
 * Parse the proto string → name, args[], locals[].
 * The SNOBOL4 label matching fn_name marks the start of the body.

/* is_fn_local: return 1 if varname is a declared param, local, or return-value
 * of the current function.  Returns 0 when in global (main) scope. */
static int is_fn_local(const char *varname) {
    if (!cur_fn_def || !varname) return 0;
    if (strcasecmp(cur_fn_def->name, varname) == 0) return 1;
    for (int i = 0; i < cur_fn_def->nargs; i++)
        if (strcasecmp(cur_fn_def->args[i], varname) == 0) return 1;
    for (int i = 0; i < cur_fn_def->nlocals; i++)
        if (strcasecmp(cur_fn_def->locals[i], varname) == 0) return 1;
    return 0;
}

/* Return fn INDEX_fn if label matches a known function entry, else -1 */
static int fn_by_label(const char *label) {
    if (!label) return -1;
    for (int i=0; i<fn_count; i++) {
        if (strcasecmp(fn_table[i].name, label)==0) return i;
        if (fn_table[i].entry_label && strcasecmp(fn_table[i].entry_label, label)==0) return i;
    }
    return -1;
}

/* Global boundary-label set: every function entry label AND every function
 * end label.  A function body stops when it hits ANY boundary label that is
 * not its own entry label. Built once after collect_fndefs() completes. */
#define BOUNDARY_MAX (FN_MAX * 2 + 8)
static char *boundary_labels[BOUNDARY_MAX];
static int   boundary_count = 0;

static void boundary_add(const char *lbl) {
    if (!lbl || !*lbl) return;
    for (int i = 0; i < boundary_count; i++)
        if (strcasecmp(boundary_labels[i], lbl) == 0) return;
    if (boundary_count < BOUNDARY_MAX)
        boundary_labels[boundary_count++] = strdup(lbl);
}

static int is_boundary_label(const char *lbl) {
    if (!lbl) return 0;
    for (int i = 0; i < boundary_count; i++)
        if (strcasecmp(boundary_labels[i], lbl) == 0) return 1;
    return 0;
}

static void build_boundary_labels(void) {
    boundary_count = 0;
    for (int i = 0; i < fn_count; i++) {
        boundary_add(fn_table[i].name);       /* entry label */
        boundary_add(fn_table[i].end_label);  /* end label (may be NULL) */
    }
}

/* Return fn INDEX_fn if stmt is the DEFINE_fn(...) call for it, else -1 */
static int fn_by_define_stmt(STMT_t *s) {
    for (int i=0; i<fn_count; i++)
        if (fn_table[i].define_stmt == s) return i;
    return -1;
}

/* Parse "fn(a,b)loc1,loc2" into a FnDef */
static void parse_proto(const char *proto, FnDef *fn) {
    /* fn name: up to '(' or end */
    int i=0;
    char buf[256];
    int j=0;
    while (proto[i] && proto[i]!='(' && proto[i]!=')') buf[j++]=proto[i++];
    buf[j]='\0';
    /* trim trailing spaces */
    while (j>0 && buf[j-1]==' ') buf[--j]='\0';
    fn->name = strdup(buf);
    fn->nargs = 0;
    fn->nlocals = 0;

    if (proto[i]=='(') {
        i++; /* skip ( */
        while (proto[i] && proto[i]!=')') {
            j=0;
            while (proto[i] && proto[i]!=',' && proto[i]!=')') buf[j++]=proto[i++];
            buf[j]='\0';
            /* trim */
            while (j>0&&(buf[j-1]==' '||buf[j-1]=='\t')) buf[--j]='\0';
            int k=0; while(buf[k]==' '||buf[k]=='\t') k++;
            if (buf[k] && fn->nargs < ARG_MAX)
                fn->args[fn->nargs++] = strdup(buf+k);
            if (proto[i]==',') i++;
        }
        if (proto[i]==')') i++;
    }

    /* locals after ')': comma-separated */
    while (proto[i]) {
        j=0;
        while (proto[i] && proto[i]!=',') buf[j++]=proto[i++];
        buf[j]='\0';
        int k=0; while(buf[k]==' '||buf[k]=='\t') k++;
        /* trim trailing */
        while (j>0&&(buf[j-1]==' '||buf[j-1]=='\t')) buf[--j]='\0';
        if (buf[k] && fn->nlocals < ARG_MAX)
            fn->locals[fn->nlocals++] = strdup(buf+k);
        if (proto[i]==',') i++;
    }
}

/* Flatten a string-literal expression (E_QLIT or E_CONCAT chain of E_QLIT)
 * into a single static buffer.  Returns NULL if any node is not a string. */
static char _define_proto_buf[4096];
static const char *flatten_str_expr(EXPR_t *e) {
    if (!e) return NULL;
    if (e->kind == E_QLIT) return e->sval;
    if (e->kind == E_CONCAT) {
        const char *l = flatten_str_expr(e->children[0]);
        const char *r = flatten_str_expr(e->children[1]);
        if (!l || !r) return NULL;
        snprintf(_define_proto_buf, sizeof _define_proto_buf, "%s%s", l, r);
        return _define_proto_buf;
    }
    return NULL;
}

/* Check if a statement is DEFINE_fn('proto') or DEFINE_fn('proto' 'locals' ...)
 * The first argument may be a chain of concatenated string literals. */
static const char *stmt_define_proto(STMT_t *s) {
    if (!s->subject) return NULL;
    EXPR_t *e = s->subject;
    if (e->kind != E_FNC) return NULL;
    if (strcasecmp(e->sval,"DEFINE")!=0) return NULL;
    if (e->nchildren < 1) return NULL;
    return flatten_str_expr(e->children[0]);
}

static void collect_fndefs(Program *prog) {
    fn_count = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        const char *proto = stmt_define_proto(s);
        if (!proto) continue;
        if (fn_count >= FN_MAX) break;
        FnDef *fn = &fn_table[fn_count];
        memset(fn, 0, sizeof *fn);
        parse_proto(proto, fn);
        fn->define_stmt = s;
        /* Extract entry label from 2nd DEFINE_fn arg: DEFINE_fn('proto','entry_label') */
        fn->entry_label = NULL;
        if (s->subject && s->subject->kind == E_FNC &&
            s->subject->nchildren >= 2) {
            const char *el = flatten_str_expr(s->subject->children[1]);
            if (el && el[0]) fn->entry_label = strdup(el);
        }
        /* Extract end-of-body label from DEFINE_fn's goto.
         * Two forms:
         *   1. DEFINE_fn('fn()')  :(FnEnd)   -- goto on same statement
         *   2. DEFINE_fn('fn()')             -- goto on the NEXT standalone statement
         *      :(FnEnd)
         */
        fn->end_label = NULL;
        if (s->go) {
            if (s->go->uncond)   fn->end_label = strdup(s->go->uncond);
            else if (s->go->onsuccess) fn->end_label = strdup(s->go->onsuccess);
        }
        if (!fn->end_label && s->next) {
            STMT_t *nxt = s->next;
            /* A standalone goto: no label being defined here, no subject, just a goto */
            if (!nxt->subject && !nxt->pattern && !nxt->replacement
                    && nxt->go && nxt->go->uncond) {
                fn->end_label = strdup(nxt->go->uncond);
            }
        }
        /* Deduplicate: if this name already exists, overwrite it.
         * SNOBOL4 function names are case-sensitive — use strcmp, not strcasecmp.
         * e.g. "Pop(var)" (stack.sno) and "POP_fn()" (semantic.sno) are DIFFERENT. */
        int found = -1;
        for (int i=0; i<fn_count; i++)
            if (strcmp(fn_table[i].name, fn->name)==0) { found=i; break; }
        if (found >= 0) {
            /* Free old name/args/locals, REPLACE_fn with new definition */
            fn_table[found] = *fn;
        } else {
            fn_count++;
        }
    }
    /* Second pass: find ALL body_starts for each function */
    for (int i=0; i<fn_count; i++) {
        fn_table[i].nbody_starts = 0;
        /* The entry label is: entry_label if set, else function name */
        const char *entry = fn_table[i].entry_label ? fn_table[i].entry_label : fn_table[i].name;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (s->label && strcasecmp(s->label, entry)==0) {
                if (fn_table[i].nbody_starts < BODY_MAX)
                    fn_table[i].body_starts[fn_table[i].nbody_starts++] = s;
            }
        }
    }
}

/* ============================================================
 * label_is_in_fn_body: return 1 if 'label' (a SNOBOL4 label) appears
 * within any body region of function fn_name.
 * If fn_name is NULL, return 1 if label appears in ANY function's body.
 * ============================================================ */
/* ============================================================
 * Body boundary rule:
 *   A function body stops when it hits a statement whose label is:
 *   (a) another DEFINE_fn'd function's entry label, OR
 *   (b) a known end_label of any function (the closing marker).
 *   Plain internal labels (io1, assign2, etc.) do NOT stop the body.
 * ============================================================ */

static int is_body_boundary(const char *label, const char *cur_fn) {
    if (!label) return 0;
    for (int i = 0; i < fn_count; i++) {
        /* The "entry" for this function is entry_label if set, else name */
        const char *entry = fn_table[i].entry_label ? fn_table[i].entry_label : fn_table[i].name;
        if (strcasecmp(entry, label) == 0 &&
            strcasecmp(fn_table[i].name, cur_fn) != 0) return 1;
        if (fn_table[i].end_label &&
            strcasecmp(fn_table[i].end_label, label) == 0) return 1;
    }
    return 0;
}

/* Return 1 if stmt s is inside the body of fn_name (NULL = any fn) */
static int stmt_in_fn_body(STMT_t *s, const char *fn_name) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_name && strcasecmp(fn_table[i].name, fn_name) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            STMT_t *bs = fn_table[i].body_starts[b];
            for (STMT_t *t = bs; t; t = t->next) {
                if (t->is_end) break;
                if (t != bs && is_body_boundary(t->label, fn_table[i].name)) break;
                if (t == s) return 1;
            }
        }
    }
    return 0;
}

/* label_is_in_fn_body: used by emit_goto_target to detect cross-fn gotos */
static int label_is_in_fn_body(const char *label, const char *fn_name) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_name && strcasecmp(fn_table[i].name, fn_name) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            STMT_t *bs = fn_table[i].body_starts[b];
            for (STMT_t *t = bs; t; t = t->next) {
                if (t->is_end) break;
                if (t != bs && is_body_boundary(t->label, fn_table[i].name)) break;
                if (t->label && strcasecmp(t->label, label) == 0) return 1;
            }
        }
    }
    return 0;
}

/* ============================================================
 * Emit header
 * ============================================================ */
static void emit_header(void) {
    C("/* generated by scrip-cc */\n");
    C("#include \"runtime_shim.h\"\n\n");
}

/* ============================================================
 * Emit global variable declarations
 * ============================================================ */
static void emit_global_var_decls(void) {
    C("/* --- global SNOBOL4 variables --- */\n");
    for (int i = 0; i < nvar; i++)
        C("static DESCR_t %s = {0};\n", cs(vars[i]));
    C("\n");
}

/* ============================================================
 * Emit one DEFINE_fn'd function
 * ============================================================ */
static void emit_fn(FnDef *fn, Program *prog) {
    /* Phantoms exist only as boundary markers — no C function to emit */
    if (!fn->define_stmt) return;
    (void)prog;
    lreg_reset();
    fn_scope_reset();   /* clear cross-pattern static-decl dedup for this fn */
    cur_fn_name = fn->name;
    cur_fn_def  = fn;
    C("static DESCR_t _sno_fn_%s(DESCR_t *_args, int _nargs) {\n", fn->name);

    /* CSNOBOL4 DEFF8/DEFF10/DEFF6 semantics: save caller's hash values on entry,
     * restore them on ALL exit paths (RETURN, FRETURN, abort/setjmp).
     * ALL SNOBOL4 variables are natural (hashed) — NEVER skip save/restore. */

    /* --- Save declarations (must come before setjmp to be in scope at restore labels) --- */
    for (int i = 0; i < fn->nargs; i++)
        C("    DESCR_t _saved_%s = NV_GET_fn(\"%s\"); /* save caller's hash value */\n",
          cs(fn->args[i]), fn->args[i]);
    for (int i = 0; i < fn->nlocals; i++)
        C("    DESCR_t _saved_%s = NV_GET_fn(\"%s\"); /* save caller's hash value */\n",
          cs(fn->locals[i]), fn->locals[i]);
    C("\n");

    C("    jmp_buf _fn_abort_jmp;\n");
    C("    if (setjmp(_fn_abort_jmp) != 0) goto _SNO_ABORT_%s;\n", fn->name);
    C("    push_abort_handler(&_fn_abort_jmp);\n\n");

    /* Return-value variable — skip if an arg has the same name */
    {
        int clash = 0;
        for (int i = 0; i < fn->nargs; i++)
            if (strcasecmp(fn->args[i], fn->name) == 0) { clash = 1; break; }
        if (!clash)
            C("    DESCR_t %s = {0}; /* return value */\n", cs(fn->name));
    }
    /* Declare C stack locals and install args into hash (DEFF8: save+assign) */
    for (int i = 0; i < fn->nargs; i++) {
        C("    DESCR_t %s = (_nargs>%d)?_args[%d]:NULVCL;\n",
          cs(fn->args[i]), i, i);
        C("    NV_SET_fn(\"%s\", %s); /* install arg in hash */\n",
          fn->args[i], cs(fn->args[i]));
    }
    /* Declare C stack locals and install as NULL into hash (DEFF10: save+null) */
    for (int i = 0; i < fn->nlocals; i++) {
        C("    DESCR_t %s = {0};\n", cs(fn->locals[i]));
        C("    NV_SET_fn(\"%s\", NULVCL); /* install local as null in hash */\n",
          fn->locals[i]);
    }
    C("\n");

    if (fn->nbody_starts == 0) {
        char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_RETURN_%s", fn->name);
        PG(_lbl);
    } else {
        STMT_t *bs = fn->body_starts[fn->nbody_starts - 1]; /* last DEFINE_fn wins */
        for (STMT_t *s = bs; s; s = s->next) {
            if (s->is_end) break;
            if (s != bs && is_body_boundary(s->label, fn->name)) break;
            cur_stmt_next_uid = next_uid();
            emit_stmt(s, fn->name);
            char _nl[64]; snprintf(_nl, sizeof _nl, "_SNO_NEXT_%d", cur_stmt_next_uid);
            PLG(_nl, "");
        }
        char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_RETURN_%s", fn->name);
        PG(_lbl);
    }

    /* --- RETURN path: restore caller's hash values (DEFF6: restore in reverse) --- */
    C("\n");
    { char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_RETURN_%s", fn->name); PLG(_lbl, ""); }
    C("    pop_abort_handler();\n");
    for (int i = fn->nlocals - 1; i >= 0; i--)
        C("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        C("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    C("    return get(%s);\n", cs(fn->name));

    /* --- FRETURN path: restore caller's hash values --- */
    { char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_FRETURN_%s", fn->name); PLG(_lbl, ""); }
    C("    pop_abort_handler();\n");
    for (int i = fn->nlocals - 1; i >= 0; i--)
        C("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        C("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    C("    return FAILDESCR;\n");

    /* --- ABORT path (setjmp fired): restore then return DT_FAIL --- */
    { char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_ABORT_%s", fn->name); PLG(_lbl, ""); }
    for (int i = fn->nlocals - 1; i >= 0; i--)
        C("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        C("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    C("    return FAILDESCR;\n");

    C("}\n\n");
}

/* ============================================================
 * Emit forward declarations for all functions
 * ============================================================ */
static void emit_fn_forwards(void) {
    for (int i = 0; i < fn_count; i++)
        C("static DESCR_t _sno_fn_%s(DESCR_t *_args, int _nargs);\n",
          fn_table[i].name);
    C("\n");
}

/* Return 1 if stmt s lies within any real (non-phantom) function body */
static int stmt_is_in_any_fn_body(STMT_t *s) {
    return stmt_in_fn_body(s, NULL);
}

/* Return 1 if stmt s lies within a phantom function's body region.
 * Phantoms have body_starts populated after injection — reuse stmt_in_fn_body. */
static int stmt_in_phantom_body(STMT_t *s) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_table[i].define_stmt) continue;  /* not a phantom */
        if (stmt_in_fn_body(s, fn_table[i].name)) return 1;
    }
    return 0;
}

static void emit_main(Program *prog) {
    lreg_reset();
    fn_scope_reset();
    cur_fn_name = "main";
    cur_fn_def  = NULL;   /* NULL = global scope; is_fn_local returns 0 for all vars */
    C("int main(void) {\n");
    C("    INIT_fn();\n");
    /* Register all global C statics so NV_SET_fn() can sync them back.
     * This bridges the two-store gap: vars set via pattern conditional
     * assignment (. varname) or pre-INIT_fn write to the hash table only;
     * registration lets those writes also update the C statics. */
    for (int i = 0; i < nvar; i++)
        C("    NV_REG_fn(\"%s\", &%s);\n", vars[i], cs(vars[i]));
    C("    NV_SYNC_fn(); /* pull pre-inited vars (nl,tab,etc) into C statics */\n");
    C("\n");

    /* Register all DEFINE_fn'd functions (skip phantoms — runtime-owned) */
    for (int i=0; i<fn_count; i++) {
        if (!fn_table[i].define_stmt) continue;  /* phantom — skip */
        /* Reconstruct the proto spec string: "name(a,b)loc1,loc2" */
        C("    DEFINE_fn(\"");
        C("%s(", fn_table[i].name);
        for (int j=0; j<fn_table[i].nargs; j++) {
            if (j) C(",");
            C("%s", fn_table[i].args[j]);
        }
        C(")");
        for (int j=0; j<fn_table[i].nlocals; j++) {
            if (j) C(","); else C("");
            C("%s", fn_table[i].locals[j]);
        }
        C("\", _sno_fn_%s);\n", fn_table[i].name);
    }
    C("\n");

    /* Emit main-level statements only */
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* END stmt — emit the end label and stop */
        if (s->is_end) {
            C("\n");
            PLG("_SNO_END", "");
            C("    finish();\n");
            C("    return 0;\n");
            C("}\n");
            return;
        }
        /* Skip statements that live inside a function body */
        if (stmt_is_in_any_fn_body(s)) continue;
        /* Skip statements inside phantom (runtime-owned) function bodies */
        if (stmt_in_phantom_body(s)) continue;
        /* Skip the DEFINE_fn(...) call statements themselves */
        if (stmt_define_proto(s)) continue;

        cur_stmt_next_uid = next_uid();
        emit_stmt(s, "main");
        { char _nl[64]; snprintf(_nl, sizeof _nl, "_SNO_NEXT_%d", cur_stmt_next_uid); PLG(_nl, ""); }
    }

    /* Fallback if no END stmt found */
}

/* ============================================================
 * Trampoline emission (sprint stmt-fn)
 *
 * emit_trampoline_program() is called instead of the classic
 * emit_fn/emit_main path when trampoline_mode=1.
 *
 * Output structure:
 *   #include "trampoline.h"
 *   #include runtime headers
 *   static DESCR_t globals...
 *   forward decls: block_X for every labeled stmt
 *   static void* stmt_N(void) { ...emit_stmt body... }  per stmt
 *   static void* block_L(void) { stmt sequence }        per labeled group
 *   int main(void) { SNO_INIT_fn(); trampoline_run(block_START); }
 * ============================================================ */

/* Collect all labels that appear on statements (block entry points) */
#define TRAMP_LABEL_MAX 4096
static char *tramp_labels[TRAMP_LABEL_MAX];
static int   tramp_nlabels = 0;

/* Collect all goto targets (may include undefined labels from library code) */
static char *tramp_goto_targets[TRAMP_LABEL_MAX];
static int   tramp_ngoto_targets = 0;

static int tramp_goto_target_known(const char *lbl) {
    for (int i = 0; i < tramp_ngoto_targets; i++)
        if (strcasecmp(tramp_goto_targets[i], lbl) == 0) return 1;
    return 0;
}

static void tramp_collect_labels(Program *prog) {
    tramp_nlabels = 0;
    tramp_ngoto_targets = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* Only collect top-level labels — skip labels inside DEFINE'd fn bodies.
         * Those are emitted as goto labels inside _sno_fn_X(), not as block fns. */
        if (s->label && tramp_nlabels < TRAMP_LABEL_MAX
                && !label_is_in_fn_body(s->label, NULL))
            tramp_labels[tramp_nlabels++] = s->label;
        /* Collect goto targets from S/F/uncond branches */
        if (s->go) {
            const char *tgts[3] = {s->go->onsuccess, s->go->onfailure, s->go->uncond};
            for (int t = 0; t < 3; t++) {
                const char *tgt = tgts[t];
                if (!tgt) continue;
                if (tgt[0] == '$') continue;  /* computed — skip */
                if (strcasecmp(tgt,"RETURN")==0 || strcasecmp(tgt,"FRETURN")==0) continue;
                if (strcasecmp(tgt,"END")==0 || strcasecmp(tgt,"START")==0) continue;
                if (tramp_goto_target_known(tgt)) continue;
                if (tramp_ngoto_targets < TRAMP_LABEL_MAX)
                    tramp_goto_targets[tramp_ngoto_targets++] = (char*)tgt;
            }
        }
    }
}

static int tramp_has_label(const char *lbl) {
    for (int i = 0; i < tramp_nlabels; i++)
        if (strcasecmp(tramp_labels[i], lbl) == 0) return 1;
    return 0;
}

static void emit_trampoline_program(Program *prog) {
    /* --- Header --- */
    C("/* generated by scrip-cc -trampoline */\n");
    C("#include \"trampoline.h\"\n");
    C("#include \"runtime_shim.h\"\n\n");

    /* --- Global variable declarations --- */
    C("/* --- global SNOBOL4 variables --- */\n");
    for (int i = 0; i < nvar; i++)
        C("static DESCR_t %s = {0};\n", cs(vars[i]));
    C("\n");

    /* --- Collect labels for forward declarations --- */
    tramp_collect_labels(prog);

    /* --- Forward declarations for all block functions --- */
    C("/* --- block forward declarations --- */\n");
    C("static void *block_START(void);\n");
    C("static void *block_END(void);\n");
    for (int i = 0; i < tramp_nlabels; i++) {
        const char *lbl = tramp_labels[i];
        if (strcasecmp(lbl,"END")==0) continue;
        if (strcasecmp(lbl,"START")==0) continue;  /* hardcoded above */
        C("static void *block%s(void);\n", cs_label(lbl));
    }
    /* Forward decls for undefined-label stubs */
    for (int i = 0; i < tramp_ngoto_targets; i++) {
        const char *tgt = tramp_goto_targets[i];
        if (tramp_has_label(tgt)) continue;
        C("static void *block%s(void);\n", cs_label(tgt));
    }
    C("\n");

    /* --- Pass 0a: pre-register ALL named pattern names FIRST ---
     * Must run before emit_fn so that *PatName inside DEFINE_fn bodies
     * (e.g. *SpecialNm in ss()) resolve to compiled functions, not
     * interpreter fallback.  Registration is just name→fnname mapping;
     * no C is emitted yet. */
    named_pat_reset();
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (!s->pattern && s->replacement &&
            s->subject && s->subject->kind == E_VAR &&
            expr_contains_pattern(s->replacement)) {
            scan_named_patterns(s->subject->sval);
        }
    }
    /* Emit struct typedecls and function fwdecls now — before emit_fn —
     * so DEFINE_fn function bodies can use pat_X_t types and call pat_X(). */
    emit_named_typedecls(out);
    emit_named_fwdecls(out);

    /* --- Emit DEFINE_fn'd functions using the existing emit_fn path ---
     * Function bodies use classic goto emission (trampoline_mode=0 inside).
     * Only main-level code uses the trampoline model. */
    {
        int saved = trampoline_mode;
        trampoline_mode = 0;
        emit_fn_forwards();
        for (int i = 0; i < fn_count; i++)
            emit_fn(&fn_table[i], prog);
        trampoline_mode = saved;
    }
    C("\n");

    /* --- Sentinel block pointers used by stmt_N as "continue" signals ---
     * _tramp_next_N is just a unique non-NULL address the block fn
     * recognises as "this stmt fell through; run next stmt". We use the
     * address of a unique static char as the sentinel. */
    /* (Generated inline per-stmt as needed — no pre-declaration required
     *  because block_L compares by value, not by name.) */

    /* --- Pass 0b/c/d: emit compiled named pattern functions ---
     * Names already pre-registered and typedecls/fwdecls already emitted
     * in pass 0a above (before emit_fn).
     * DO NOT call named_pat_reset() or re-emit typedecls here.
     */
    C("/* --- compiled named pattern function bodies --- */\n");

    /* 0d: emit function bodies (emitted flag prevents duplicates) */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (!s->pattern && s->replacement &&
            s->subject && s->subject->kind == E_VAR &&
            expr_contains_pattern(s->replacement)) {
            emit_named_pattern(s->subject->sval, s->replacement, out);
        }
    }
    C("\n");

    /* --- Emit each main-level stmt as its own function --- */
    lreg_reset();
    fn_scope_reset();
    cur_fn_name = "main";
    cur_fn_def  = NULL;

    /* sid_uid[sid] = the cur_stmt_next_uid assigned to stmt sid (1-based) */
#define TRAMP_STMT_MAX 8192
    static int sid_uid[TRAMP_STMT_MAX];
    static STMT_t *sid_stmt[TRAMP_STMT_MAX];
    int stmt_count = 0;

    /* Pass 1: emit stmt_N() functions, record sid→uid mapping */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_is_in_any_fn_body(s)) continue;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (stmt_count >= TRAMP_STMT_MAX) break;

        int sid = ++stmt_count;
        cur_stmt_next_uid = next_uid();
        sid_uid[sid]  = cur_stmt_next_uid;
        sid_stmt[sid] = s;

        /* sentinel: unique static char address == "this stmt fell through" */
        C("static char _tramp_sentinel_%d;\n", cur_stmt_next_uid);
        C("#define _tramp_next_%d ((void*)&_tramp_sentinel_%d)\n",
          cur_stmt_next_uid, cur_stmt_next_uid);

        C("static void *stmt_%d(void) { /* line %d%s%s */\n",
          sid, s->lineno,
          s->label ? " label:" : "",
          s->label ? s->label  : "");
        emit_stmt(s, "main");
        C("}\n\n");
    }

    /* Pass 2: emit block grouping functions.
     * Rule: a labeled stmt starts a new block.
     * Unlabeled stmts after it belong to the same block.
     * Each block calls its member stmts; if stmt returns _tramp_next_N
     * (fall-through sentinel), continue; otherwise return immediately.
     */
    C("/* --- block functions --- */\n");

    int block_open = 0;
    int first_block = 1;  /* first block is always block_START */
    const char *first_block_label = NULL; /* label on first stmt, needs alias */

    for (int sid = 1; sid <= stmt_count; sid++) {
        STMT_t *s = sid_stmt[sid];

        /* A labeled stmt closes the current block (falls through to new label) */
        if (s->label && block_open) {
            C("    return block%s; /* fall into next block */\n}\n\n",
              cs_label(s->label));
            block_open = 0;
        }

        /* Open a new block if none is open */
        if (!block_open) {
            if (first_block) {
                C("static void *block_START(void) {\n");
                first_block = 0;
                /* If first stmt carries a label, record it for alias emission */
                if (s->label) first_block_label = s->label;
                /* (alias emitted at block close — see first_block_label below) */
            } else if (s->label) {
                C("static void *block%s(void) {\n", cs_label(s->label));
            } else {
                /* Unlabeled stmt after a fall-through gap — shouldn't normally
                 * happen in well-formed SNOBOL4, but handle gracefully */
                C("static void *block_START(void) {\n");
            }
            block_open = 1;
        }

        /* Call stmt, propagate any non-fallthrough return */
        C("    { void *_r = stmt_%d();\n", sid);
        C("      if (_r != _tramp_next_%d) return _r; }\n", sid_uid[sid]);
    }

    /* Close the last open block */
    if (block_open) {
        C("    return block_END;\n}\n\n");
    } else if (stmt_count == 0) {
        /* Empty program */
        C("static void *block_START(void) { return block_END; }\n\n");
    }

    /* If first stmt had a label other than START, block_START absorbed it
     * but block_<label> was forward-declared. Emit a forwarding alias. */
    if (first_block_label && strcasecmp(first_block_label, "START") != 0) {
        C("static void *block%s(void) { return block_START(); }\n\n",
          cs_label(first_block_label));
    }

    C("static void *block_END(void) { return NULL; }\n\n");

    /* --- Stubs for undefined labels (e.g. 'err' from library code) --- */
    C("/* --- undefined label stubs --- */\n");
    for (int i = 0; i < tramp_ngoto_targets; i++) {
        const char *tgt = tramp_goto_targets[i];
        if (tramp_has_label(tgt)) continue;
        C("static void *block%s(void) { return NULL; }\n", cs_label(tgt));
    }
    C("\n");

    /* --- _block_label_table: label string -> block fn pointer --- */
    C("/* --- computed-goto label table --- */\n");
    C("_BlockEntry_t _block_label_table[] = {\n");
    C("    {\"START\", block_START},\n");
    C("    {\"END\",   block_END},\n");
    for (int i = 0; i < tramp_nlabels; i++) {
        const char *lbl = tramp_labels[i];
        if (strcasecmp(lbl,"END")==0) continue;
        if (strcasecmp(lbl,"START")==0) continue;  /* hardcoded above */
        C("    {\"%s\", block%s},\n", lbl, cs_label(lbl));
    }
    /* Add undefined-label stubs to the table too */
    for (int i = 0; i < tramp_ngoto_targets; i++) {
        const char *tgt = tramp_goto_targets[i];
        if (tramp_has_label(tgt)) continue;
        C("    {\"%s\", block%s},\n", tgt, cs_label(tgt));
    }
    C("};\n");
    C("int _block_label_count = (int)(sizeof(_block_label_table)/sizeof(_block_label_table[0]));\n\n");

    /* --- main --- */
    C("int main(void) {\n");
    C("    INIT_fn();\n");
    for (int i = 0; i < nvar; i++)
        C("    NV_REG_fn(\"%s\", &%s);\n", vars[i], cs(vars[i]));
    C("    NV_SYNC_fn();\n\n");
    for (int i = 0; i < fn_count; i++) {
        if (!fn_table[i].define_stmt) continue;
        C("    DEFINE_fn(\"%s(", fn_table[i].name);
        for (int j = 0; j < fn_table[i].nargs; j++) {
            if (j) C(",");
            C("%s", fn_table[i].args[j]);
        }
        C(")");
        for (int j = 0; j < fn_table[i].nlocals; j++) {
            if (j) C(","); else C("");
            C("%s", fn_table[i].locals[j]);
        }
        C("\", _sno_fn_%s);\n", fn_table[i].name);
    }
    /* Initialize well-known globals that SNOBOL4 programs assume are set
     * by the runtime but are not defined in beauty.sno's inc files.
     * nl = CHAR(10), tab = CHAR(9) */
    C("\n    /* runtime globals */\n");
    C("    NV_SET_fn(\"nl\",  APPLY_fn(\"CHAR\",(DESCR_t[]){INTVAL(10)},1));\n");
    C("    NV_SET_fn(\"tab\", APPLY_fn(\"CHAR\",(DESCR_t[]){INTVAL(9)},1));\n");
    /* Fix: DATA('tree(...)') and DATA('link(...)') land in dead code inside
     * _sno_fn_Top — tree.sno init block swallowed by StackEnd boundary.
     * Register explicitly here so tree()/link() are live before trampoline. */
    C("    /* DATA types from tree.sno/stack.sno (fn-body-walk bug) */\n");
    C("    APPLY_fn(\"DATA\",(DESCR_t[]){STRVAL(\"tree(t,v,n,c)\")},1);\n");
    C("    APPLY_fn(\"DATA\",(DESCR_t[]){STRVAL(\"link(next,value)\")},1);\n");
    C("\n    trampoline_run(block_START);\n");
    C("    return 0;\n}\n");
}

/* ============================================================
 * Public entry point
 * ============================================================ */
void c_emit(Program *prog, FILE *f) {
    out = f;

    /* Phase 1: collect variable names and function definitions */
    collect_vars(prog);
    collect_fndefs(prog);

    /* DEBUG: dump fn_table after collect */
    if (getenv("SNOC_FN_DEBUG")) {
        for (int _di = 0; _di < fn_count; _di++)
            fprintf(stderr, "FN[%d] name=%-20s entry=%-12s end=%-14s nbody=%d\n",
                _di, fn_table[_di].name,
                fn_table[_di].entry_label ? fn_table[_di].entry_label : "(null)",
                fn_table[_di].end_label   ? fn_table[_di].end_label   : "(null)",
                fn_table[_di].nbody_starts);
    }

    /* Phase 1b: inject phantom FnDef entries for every runtime-owned function
     * whose source body appears in the expanded -INCLUDE stream but whose DEFINE_fn
     * is handled by mock_includes.c at runtime (so collect_functions never sees it).
     *
     * Phantoms have name + end_label only. define_stmt = NULL, nbody_starts = 0.
     * They exist SOLELY so is_body_boundary() recognises their entry/end labels
     * as boundaries and stops body-absorption into the wrong C function.
     * emit_fn() skips phantoms (define_stmt == NULL → no C function emitted).
     * emit_main() skips phantoms (define_stmt == NULL → no DEFINE_fn() call).
     *
     * Source: mock_includes.c inc_init() + inc_init_extra() registrations
     * whose bodies appear in: ShiftReduce.sno, stack.sno, counter.sno, semantic.sno
     */
    static const struct { const char *name; const char *end_label; } phantoms[] = {
        /* ShiftReduce.sno */
        { "Shift",        "ShiftEnd"    },
        { "Reduce",       "ReduceEnd"   },
        /* stack.sno */
        { "InitStack",    "StackEnd"    },
        { "Push",         "StackEnd"    },
        { "Pop",          "StackEnd"    },
        { "Top",          "StackEnd"    },
        /* counter.sno */
        { "InitCounter",  "CounterEnd"  },
        { "PushCounter",  "CounterEnd"  },
        { "IncCounter",   "CounterEnd"  },
        { "DecCounter",   "CounterEnd"  },
        { "PopCounter",   "CounterEnd"  },
        { "TopCounter",   "CounterEnd"  },
        { "InitBegTag",   "BegTagEnd"   },
        { "PushBegTag",   "BegTagEnd"   },
        { "PopBegTag",    "BegTagEnd"   },
        { "TopBegTag",    "BegTagEnd"   },
        { "DumpBegTag",   "BegTagEnd"   },
        { "InitEndTag",   "EndTagEnd"   },
        { "PushEndTag",   "EndTagEnd"   },
        { "PopEndTag",    "EndTagEnd"   },
        { "TopEndTag",    "EndTagEnd"   },
        { "DumpEndTag",   "EndTagEnd"   },
        /* semantic.sno — entry labels are name_, end is semanticEnd */
        { "shift_",       "semanticEnd" },
        { "reduce_",      "semanticEnd" },
        { "pop_",         "semanticEnd" },
        { "nPush_",       "semanticEnd" },
        { "nInc_",        "semanticEnd" },
        { "nDec_",        "semanticEnd" },
        { "nTop_",        "semanticEnd" },
        { "nPop_",        "semanticEnd" },
        { NULL, NULL }
    };
    for (int pi = 0; phantoms[pi].name && fn_count < FN_MAX; pi++) {
        /* Skip if already in fn_table (defined by SNOBOL4 DEFINE_fn in-stream).
         * Two-arg DEFINE_fn('nInc()', 'nInc_') stores name="nInc", entry_label="nInc_".
         * Phantom name is "nInc_" — must check entry_label to avoid double-registering. */
        int already = 0;
        for (int fi = 0; fi < fn_count; fi++)
            if (strcmp(fn_table[fi].name, phantoms[pi].name) == 0 ||
                (fn_table[fi].entry_label &&
                 strcmp(fn_table[fi].entry_label, phantoms[pi].name) == 0))
            { already=1; break; }
        if (already) continue;
        FnDef *ph = &fn_table[fn_count++];
        memset(ph, 0, sizeof *ph);
        ph->name        = strdup(phantoms[pi].name);
        ph->end_label   = strdup(phantoms[pi].end_label);
        ph->define_stmt = NULL;   /* phantom: no SNOBOL4 DEFINE_fn, no C emission */
        ph->entry_label = NULL;
        ph->nbody_starts = 0;
    }

    /* Populate body_starts for phantoms by scanning the program for their entry label.
     * This lets stmt_in_fn_body() work for phantoms exactly like real functions. */
    for (int i = 0; i < fn_count; i++) {
        if (fn_table[i].define_stmt) continue;  /* not a phantom */
        const char *entry = fn_table[i].entry_label
                          ? fn_table[i].entry_label
                          : fn_table[i].name;
        fn_table[i].nbody_starts = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (s->label && strcmp(s->label, entry) == 0) {
                if (fn_table[i].nbody_starts < BODY_MAX)
                    fn_table[i].body_starts[fn_table[i].nbody_starts++] = s;
            }
        }
    }

    /* Phase 2: emit */
    if (trampoline_mode) {
        emit_trampoline_program(prog);
        return;
    }
    emit_header();
    emit_global_var_decls();
    emit_fn_forwards();

    for (int i=0; i<fn_count; i++)
        emit_fn(&fn_table[i], prog);

    emit_main(prog);
}
