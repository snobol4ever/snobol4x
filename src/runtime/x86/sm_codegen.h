/*
 * sm_codegen.h — SM_Program → x86 in-memory code (M-JIT-RUN)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date: 2026-04-07
 */

#ifndef SM_CODEGEN_H
#define SM_CODEGEN_H

#include "sm_prog.h"
#include "sm_interp.h"   /* SM_State */
#include <setjmp.h>

/*
 * sm_codegen — compile prog into SEG_DISPATCH + SEG_CODE.
 * Must be called after sm_image_init().
 * Returns 0 on success, -1 on error.
 */
int sm_codegen(SM_Program *prog);

/*
 * sm_jit_run — execute the codegen'd program.
 * Must be called after sm_codegen().
 * Returns 0 on normal halt.
 */
int sm_jit_run(SM_Program *prog, SM_State *st);

int sm_jit_run_plain(SM_Program *prog, SM_State *st);

/* IM-5: step-limit — run at most n statements then return */
extern int     g_jit_step_limit;
extern int     g_jit_steps_done;
extern jmp_buf g_jit_step_jmp;
int sm_jit_run_steps(SM_Program *prog, SM_State *st, int n);

#endif /* SM_CODEGEN_H */
