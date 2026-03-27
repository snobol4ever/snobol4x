CC     = gcc
CFLAGS = -Wall -Wno-unused-function -g -O0 \
         -I frontend/snobol4 \
         -I frontend/snocone \
         -I frontend/prolog \
         -I backend/c \
         -I backend/x64

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
    frontend/prolog/prolog_emit_jvm.c \
    frontend/prolog/prolog_builtin.c

FRONTEND_SNOCONE = \
    frontend/snocone/snocone_lex.c \
    frontend/snocone/snocone_parse.c \
    frontend/snocone/snocone_lower.c \
    frontend/snocone/snocone_driver.c \
    frontend/snocone/snocone_cf.c

BACKEND_C = \
    backend/c/emit_cnode.c \
    backend/c/emit_byrd_c.c

BACKEND_X64 = \
    backend/x64/emit_byrd_asm.c

DRIVER = \
    driver/main.c

BACKEND_JVM = \
    backend/jvm/emit_byrd_jvm.c

BACKEND_NET = \
    backend/net/emit_byrd_net.c

SRCS = $(FRONTEND_SNO) $(FRONTEND_PROLOG) $(FRONTEND_SNOCONE) $(BACKEND_C) $(BACKEND_X64) $(BACKEND_JVM) $(BACKEND_NET) $(DRIVER)
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

BIN    = ../sno2c

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Pattern rule — each .c depends on the shared headers
%.o: %.c \
     frontend/snobol4/sno2c.h \
     frontend/snobol4/lex.h \
     backend/c/emit_cnode.h \
     backend/c/emit_pretty.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS)

# icon_driver — Icon JVM frontend (standalone binary)
ICON_SRCS = \
    frontend/icon/icon_driver.c \
    frontend/icon/icon_lex.c \
    frontend/icon/icon_parse.c \
    frontend/icon/icon_ast.c \
    frontend/icon/icon_emit.c \
    frontend/icon/icon_emit_jvm.c \
    frontend/icon/icon_runtime.c

ICON_BIN = ../icon_driver

icon_driver: $(ICON_BIN)

$(ICON_BIN): $(ICON_SRCS)
	$(CC) $(CFLAGS) -I frontend/icon -o $@ $^

.PHONY: all clean icon_driver
