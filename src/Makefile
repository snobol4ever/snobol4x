CC     = gcc
CFLAGS = -Wall -Wno-unused-function -g -O0 \
         -I frontend/snobol4 \
         -I ir/byrd \
         -I backend/c \
         -I backend/x64

FRONTEND_SNO = \
    frontend/snobol4/lex.c \
    frontend/snobol4/parse.c

IR_BYRD = \
    ir/byrd/emit_cnode.c

BACKEND_C = \
    backend/c/emit.c \
    backend/c/emit_byrd.c

BACKEND_X64 = \
    backend/x64/emit_byrd_asm.c

DRIVER = \
    driver/main.c

SRCS = $(FRONTEND_SNO) $(IR_BYRD) $(BACKEND_C) $(BACKEND_X64) $(DRIVER)
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
     ir/byrd/emit_cnode.h \
     backend/c/emit_pretty.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS)
