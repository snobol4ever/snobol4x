/*
 * sm_prog.h — SM_Instr / SM_Program types (M-SCRIP-U2)
 *
 * The flat instruction array that SM-LOWER produces from IR.
 * Both the interpreter dispatch loop and the in-memory code generator
 * walk this same array — one dispatches in C, the other emits x86 blobs.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date: 2026-04-06
 */

#ifndef SM_PROG_H
#define SM_PROG_H

#include <stdint.h>
#include <stddef.h>

/* ── Opcode enumeration ─────────────────────────────────────────────── */

typedef enum {
    /* Control */
    SM_LABEL = 0,
    SM_JUMP,
    SM_JUMP_S,
    SM_JUMP_F,
    SM_HALT,
    SM_STNO,       /* increment &STCOUNT/&STNO at each statement boundary */

    /* Values */
    SM_PUSH_LIT_S,
    SM_PUSH_LIT_I,
    SM_PUSH_LIT_F,
    SM_PUSH_NULL,
    SM_PUSH_VAR,
    SM_STORE_VAR,
    SM_POP,

    /* Arithmetic / String */
    SM_ADD,
    SM_SUB,
    SM_MUL,
    SM_DIV,
    SM_EXP,
    SM_CONCAT,
    SM_COERCE_NUM, /* unary +: coerce top of stack to int or real */
    SM_NEG,

    /* Pattern construction */
    SM_PAT_LIT,
    SM_PAT_ANY,
    SM_PAT_NOTANY,
    SM_PAT_SPAN,
    SM_PAT_BREAK,
    SM_PAT_LEN,
    SM_PAT_POS,
    SM_PAT_RPOS,
    SM_PAT_TAB,
    SM_PAT_RTAB,
    SM_PAT_ARB,
    SM_PAT_REM,
    SM_PAT_BAL,
    SM_PAT_FENCE,
    SM_PAT_ABORT,
    SM_PAT_FAIL,
    SM_PAT_SUCCEED,
    SM_PAT_EPS,         /* push epsilon pattern onto pat-stack */
    SM_PAT_ALT,
    SM_PAT_CAT,
    SM_PAT_DEREF,
    SM_PAT_CAPTURE,

    /* Statement execution */
    SM_EXEC_STMT,

    /* Functions */
    SM_CALL,
    SM_RETURN,
    SM_FRETURN,
    SM_NRETURN,
    SM_DEFINE,

    /* Type dispatch / indirect */
    SM_JUMP_INDIR,
    SM_SELBRA,

    /* State save/restore */
    SM_STATE_PUSH,
    SM_STATE_POP,

    /* Integer arithmetic */
    SM_INCR,
    SM_DECR,
    SM_LCOMP,
    SM_RCOMP,
    SM_TRIM,
    SM_ACOMP,
    SM_SPCINT,
    SM_SPREAL,

    SM_PAT_BOXVAL,  /* pop pat-stack top → push as DT_P onto value-stack */

    SM_OPCODE_COUNT
} sm_opcode_t;

/* ── Instruction operand union ──────────────────────────────────────── */

typedef union {
    int64_t     i;          /* integer literal / label index / nargs */
    double      f;          /* float literal */
    const char *s;          /* string literal / variable name / label name */
    int         b;          /* boolean (has_repl etc.) */
} sm_operand_t;

/* ── Single instruction ─────────────────────────────────────────────── */

#define SM_MAX_OPERANDS 3

typedef struct {
    sm_opcode_t   op;
    sm_operand_t  a[SM_MAX_OPERANDS];   /* a[0], a[1], a[2] */
} SM_Instr;

/* ── Program (flat array of instructions) ───────────────────────────── */

typedef struct {
    SM_Instr *instrs;
    int       count;
    int       cap;
} SM_Program;

/* ── Builder helpers ────────────────────────────────────────────────── */

SM_Program *sm_prog_new(void);
void        sm_prog_free(SM_Program *p);

/* Append one instruction; returns its index */
int sm_emit(SM_Program *p, sm_opcode_t op);
int sm_emit_s(SM_Program *p, sm_opcode_t op, const char *s);
int sm_emit_i(SM_Program *p, sm_opcode_t op, int64_t i);
int sm_emit_f(SM_Program *p, sm_opcode_t op, double f);
int sm_emit_si(SM_Program *p, sm_opcode_t op, const char *s, int64_t i);
int sm_emit_ii(SM_Program *p, sm_opcode_t op, int64_t i0, int64_t i1);

/* Label: emit SM_LABEL with index=next_instr; return the label index */
int sm_label(SM_Program *p);

/* Patch a jump target: set a[0].i of instr at `jump_idx` to `target_label` */
void sm_patch_jump(SM_Program *p, int jump_idx, int target_label);

/* Opcode name for diagnostics */
const char *sm_opcode_name(sm_opcode_t op);

#endif /* SM_PROG_H */
