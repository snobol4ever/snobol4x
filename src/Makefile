CC     = gcc
CFLAGS = -Wall -Wno-unused-function -g -O0 \
         -I frontend/snobol4 \
         -I frontend/snocone \
         -I backend/c \
         -I backend/x64

FRONTEND_SNO = \
    frontend/snobol4/lex.c \
    frontend/snobol4/parse.c

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

SRCS = $(FRONTEND_SNO) $(FRONTEND_SNOCONE) $(BACKEND_C) $(BACKEND_X64) $(BACKEND_JVM) $(BACKEND_NET) $(DRIVER)
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
