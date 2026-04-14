CC     = gcc
CFLAGS = -Wall -Wno-unused-function -g -O0 \
         -I . \
         -I frontend/snobol4 \
         -I frontend/snocone \
         -I frontend/prolog \
         -I frontend/icon \
         -I frontend/rebus \
         -I frontend/raku \
         -I runtime/x86

FRONTEND_SNO = \
    frontend/snobol4/snobol4.tab.c \
    frontend/snobol4/snobol4.lex.c

FRONTEND_PROLOG = \
    frontend/prolog/prolog_atom.c \
    frontend/prolog/prolog_lex.c \
    frontend/prolog/prolog_parse.c \
    frontend/prolog/prolog_unify.c \
    frontend/prolog/prolog_lower.c \
    frontend/prolog/prolog_builtin.c \
    frontend/prolog/prolog_driver.c \
    frontend/prolog/pl_broker.c

FRONTEND_SNOCONE = \
    frontend/snocone/snocone_lex.c \
    frontend/snocone/snocone_parse.c \
    frontend/snocone/snocone_driver.c \
    frontend/snocone/snocone_lower.c \
    frontend/snocone/snocone_cf.c

FRONTEND_REBUS = \
    frontend/rebus/rebus_lower.c \
    frontend/rebus/rebus.tab.c \
    frontend/rebus/lex.rebus.c \
    frontend/rebus/rebus_print.c

FRONTEND_ICON = \
    frontend/icon/icn_main.c \
    frontend/icon/icon_lex.c \
    frontend/icon/icon_parse.c \
    frontend/icon/icon_runtime.c \
    frontend/icon/icon_driver.c \
    frontend/icon/icon_gen.c

IR = \
    ir/ir_print.c \
    ir/ir_verify.c

RUNTIME_X86 = \
    runtime/x86/snobol4.c \
    runtime/x86/snobol4_argval.c \
    runtime/x86/snobol4_invoke.c \
    runtime/x86/snobol4_nmd.c \
    runtime/x86/snobol4_pattern.c \
    runtime/x86/snobol4_stmt_rt.c \
    runtime/x86/stmt_exec.c \
    runtime/x86/bb_boxes.c \
    runtime/x86/bb_broker.c \
    runtime/x86/bb_build.c \
    runtime/x86/bb_emit.c \
    runtime/x86/bb_flat.c \
    runtime/x86/bb_pool.c \
    runtime/x86/engine.c \
    runtime/x86/eval_code.c \
    runtime/x86/sm_lower.c \
    runtime/x86/sm_interp.c \
    runtime/x86/sm_prog.c \
    runtime/x86/sm_codegen.c \
    runtime/x86/sm_image.c

FRONTEND_RAKU = \
    frontend/raku/raku.tab.c \
    frontend/raku/raku.lex.c \
    frontend/raku/raku_driver.c

ICN_RUNTIME = \
    runtime/interp/icn_runtime.c

PL_RUNTIME = \
    runtime/interp/pl_runtime.c

DRIVER = \
    driver/interp.c \
    driver/polyglot.c \
    driver/sync_monitor.c \
    driver/scrip.c

SRCS = $(FRONTEND_SNO) $(FRONTEND_PROLOG) $(FRONTEND_SNOCONE) $(FRONTEND_REBUS) \
       $(FRONTEND_ICON) $(FRONTEND_RAKU) $(ICN_RUNTIME) $(PL_RUNTIME) $(IR) $(RUNTIME_X86) $(DRIVER)
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

BIN = ../scrip

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lgc

# Rebus parser — generated files are committed; only regenerate if absent.
frontend/rebus/rebus.tab.c frontend/rebus/rebus.tab.h: | frontend/rebus/rebus.y
	@if [ ! -f frontend/rebus/rebus.tab.c ] || [ ! -f frontend/rebus/rebus.tab.h ]; then \
	    bison -d -o frontend/rebus/rebus.tab.c frontend/rebus/rebus.y; \
	fi

frontend/rebus/lex.rebus.c: | frontend/rebus/rebus.l frontend/rebus/rebus.tab.h
	@if [ ! -f frontend/rebus/lex.rebus.c ]; then \
	    flex -o frontend/rebus/lex.rebus.c frontend/rebus/rebus.l; \
	fi

frontend/snobol4/snobol4.lex.c: | frontend/snobol4/snobol4.l
	@if [ ! -f frontend/snobol4/snobol4.lex.c ]; then \
	    flex -o frontend/snobol4/snobol4.lex.c frontend/snobol4/snobol4.l; \
	fi

frontend/rebus/rebus.tab.o frontend/rebus/lex.rebus.o \
frontend/rebus/rebus_lower.o frontend/rebus/rebus_print.o: frontend/rebus/rebus.tab.h

# Pattern rule — each .c compiled with shared headers as implicit deps
%.o: %.c \
     frontend/snobol4/scrip_cc.h \
     frontend/snobol4/snobol4.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS)

debug: CFLAGS += -DDEBUG
debug: $(BIN)
