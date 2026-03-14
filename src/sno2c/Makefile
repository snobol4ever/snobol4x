CC     = gcc
CFLAGS = -Wall -Wno-unused-function -g -O0 -I.

SRCS   = lex.c parse.c emit.c emit_byrd.c emit_cnode.c main.c
OBJS   = $(SRCS:.c=.o)

.PHONY: all clean

all: sno2c

sno2c: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c sno2c.h lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f sno2c $(OBJS)
