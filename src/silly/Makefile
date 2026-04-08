# Makefile — silly-snobol4 faithful C rewrite of v311.sil
# Usage:
#   make              → build /tmp/silly-snobol4  (debug)
#   make release      → build /tmp/silly-snobol4  (optimised)
#   make clean        → remove object files
#   make check        → build + smoke test (run with no args, expect usage/exit)

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -O0 -I.
RFLAGS  = -Wall -Wextra -std=c99 -O2    -I.
LDFLAGS = -lm

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

TARGET = /tmp/silly-snobol4

.PHONY: all release clean check

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

release:
	$(CC) $(RFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(OBJS)

check: $(TARGET)
	@echo "=== silly-snobol4 smoke test ==="
	@echo 'OUTPUT = "hello"' | $(TARGET) && echo "PASS" || echo "FAIL (exit $$?)"
