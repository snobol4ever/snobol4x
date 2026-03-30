CC     = gcc
CFLAGS = -Wall -Wno-unused-function -g -O0 \
         -I . \
         -I frontend/snobol4 \
         -I frontend/snocone \
         -I frontend/prolog \
         -I frontend/icon \
         -I frontend/rebus \
         -I backend/c \
         -I backend

FRONTEND_SNO = \
    frontend/snobol4/lex.c \
    frontend/snobol4/parse.c

FRONTEND_PROLOG = \
    frontend/prolog/prolog_atom.c \
    frontend/prolog/prolog_lex.c \
    frontend/prolog/prolog_parse.c \
    frontend/prolog/prolog_unify.c \
    frontend/prolog/prolog_lower.c \
    frontend/prolog/prolog_emit.c \
    frontend/prolog/prolog_emit_net.c \
    frontend/prolog/prolog_builtin.c

FRONTEND_SNOCONE = \
    frontend/snocone/snocone_lex.c \
    frontend/snocone/snocone_parse.c

FRONTEND_REBUS = \
    frontend/rebus/rebus_lower.c \
    frontend/rebus/rebus.tab.c \
    frontend/rebus/lex.rebus.c \
    frontend/rebus/rebus_print.c

BACKEND_C = \
    backend/c/emit_cnode.c \
    backend/c/emit_byrd_c.c

BACKEND_X64 = \
    backend/emit_x64.c \
    backend/emit_x64_icon.c \
    backend/emit_x64_snocone.c


DRIVER = \
    driver/main.c

BACKEND_JVM = \
    backend/emit_jvm.c \
    backend/emit_jvm_icon.c \
    backend/emit_jvm_prolog.c

BACKEND_NET = \
    backend/emit_net.c

BACKEND_WASM = \
    backend/emit_wasm.c \
    backend/emit_wasm_prolog.c

IR = \
    ir/ir_print.c \
    ir/ir_verify.c \
    ir/ir_emit_common.c

FRONTEND_ICON = \
    frontend/icon/icn_main.c \
    frontend/icon/icon_lex.c \
    frontend/icon/icon_parse.c \
    frontend/icon/icon_ast.c \
    frontend/icon/icon_runtime.c

SRCS = $(FRONTEND_SNO) $(FRONTEND_PROLOG) $(FRONTEND_SNOCONE) $(FRONTEND_REBUS) $(FRONTEND_ICON) $(BACKEND_C) $(BACKEND_X64) $(BACKEND_JVM) $(BACKEND_NET) $(BACKEND_WASM) $(IR) $(DRIVER)
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

BIN    = ../scrip-cc

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Rebus parser — generated files are committed; only regenerate if absent.
# Order-only prerequisites (after |) are checked for existence, not timestamp.
# This prevents bison/flex from being invoked on a fresh clone where .y/.l
# appear newer than the generated files due to git not preserving mtimes.
frontend/rebus/rebus.tab.c frontend/rebus/rebus.tab.h: | frontend/rebus/rebus.y
	@if [ ! -f frontend/rebus/rebus.tab.c ] || [ ! -f frontend/rebus/rebus.tab.h ]; then \
	    bison -d -o frontend/rebus/rebus.tab.c frontend/rebus/rebus.y; \
	fi

frontend/rebus/lex.rebus.c: | frontend/rebus/rebus.l frontend/rebus/rebus.tab.h
	@if [ ! -f frontend/rebus/lex.rebus.c ]; then \
	    flex -o frontend/rebus/lex.rebus.c frontend/rebus/rebus.l; \
	fi

frontend/rebus/rebus.tab.o frontend/rebus/lex.rebus.o frontend/rebus/rebus_lower.o frontend/rebus/rebus_print.o: frontend/rebus/rebus.tab.h

# emit_x64_prolog.c is included by emit_x64.c — explicit dep so make rebuilds on change
backend/emit_x64.o: backend/emit_x64_prolog.c

# Pattern rule — each .c depends on the shared headers
%.o: %.c \
     frontend/snobol4/scrip_cc.h \
     frontend/snobol4/lex.h \
     backend/c/emit_cnode.h \
     backend/c/emit_pretty.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS)

debug: CFLAGS += -DDEBUG
debug: $(BIN)

