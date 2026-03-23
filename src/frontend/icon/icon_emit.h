/*
 * icon_emit.h — Tiny-ICON four-port x64 ASM emitter API
 *
 * Translates an IcnNode AST to NASM x64 assembly using the Byrd Box
 * four-port model (α/β/γ/ω) as described in Proebsting 1996.
 *
 * Label convention (matches existing snobol4x ASM backend):
 *   icon_N_a   α — initial entry (start)
 *   icon_N_b   β — re-entry (resume)
 *   icon_N_g   γ — success departure
 *   icon_N_w   ω — failure departure
 * where N is a unique node ID assigned during emission.
 *
 * Extra labels (e.g. for ICN_TO counter check):
 *   icon_N_code   (intermediate logic label)
 *
 * Port threading:
 *   α and β are synthesized (emitted inline).
 *   γ and ω are inherited — passed as label strings from the parent node.
 *
 * Bounded flag:
 *   All four ports emitted unconditionally (bounded optimization deferred).
 */

#ifndef ICON_EMIT_H
#define ICON_EMIT_H

#include "icon_ast.h"
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Port set: inherited ports passed down from parent to child
 * -------------------------------------------------------------------------*/
typedef struct {
    char succeed[64];  /* γ — where to jump on success */
    char fail[64];     /* ω — where to jump on failure */
} IcnPorts;

/* -------------------------------------------------------------------------
 * Emitter state
 * -------------------------------------------------------------------------*/
typedef struct {
    FILE *out;          /* output stream */
    int   node_id;      /* monotonic counter for unique label generation */
    int   bounded;      /* non-zero = value-needed context (deferred) */
    char  errmsg[256];
    int   had_error;
} IcnEmitter;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Initialize emitter writing to 'out' */
void icn_emit_init(IcnEmitter *em, FILE *out);

/* Emit the full assembly for a list of top-level nodes (procedures + globals).
 * This is the main entry point called by icon_driver.c */
void icn_emit_file(IcnEmitter *em, IcnNode **nodes, int count);

/* Emit a single procedure */
void icn_emit_proc(IcnEmitter *em, IcnNode *proc);

/* Emit one expression node with given inherited ports.
 * Fills 'out_alpha' and 'out_beta' with the synthesized port labels.
 * Callers chain: parent's γ/ω → child's constructor → child emits, returns α/β labels. */
void icn_emit_expr(IcnEmitter *em, IcnNode *n,
                   IcnPorts ports,
                   char *out_alpha, char *out_beta);

/* Allocate a unique node ID and fill label strings */
int  icn_new_id(IcnEmitter *em);
void icn_label_alpha(int id, char *buf, size_t sz);
void icn_label_beta (int id, char *buf, size_t sz);
void icn_label_code (int id, char *buf, size_t sz);  /* extra: to.code etc */

#endif /* ICON_EMIT_H */
