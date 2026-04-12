#ifndef ICON_INTERP_H
#define ICON_INTERP_H
/*
 * icon_interp.h — Icon IR interpreter
 *
 * Mirrors emit_x64.c (Icon section) using the same Byrd box four-port model.
 * Entry point: icon_execute_program(prog)
 */
#include "../snobol4/scrip_cc.h"
void icon_execute_program(Program *prog);
#endif
