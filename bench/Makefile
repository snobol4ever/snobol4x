# SNOBOL4-tiny Benchmark Suite
# Run: make run
# Requires: libpcre2-dev, bison, python3

CC     = cc
CFLAGS = -O2
PCRE2  = $(shell pcre2-config --libs8)

.PHONY: all run re pda clean

all: bench_re bench_pda

bench_re: bench_re_vs_tiny.c
	$(CC) $(CFLAGS) -o $@ $< $(PCRE2)

pda/anbn_renamed.c: pda/anbn.y
	bison -d -o pda/anbn.tab.c pda/anbn.y
	python3 rename_bison.py pda/anbn.tab.c anbn pda/anbn_renamed.c

pda/dyck_renamed.c: pda/dyck.y
	bison -d -o pda/dyck.tab.c pda/dyck.y
	python3 rename_bison.py pda/dyck.tab.c dyck pda/dyck_renamed.c

bench_pda: pda/bench_pda.c pda/anbn_renamed.c pda/dyck_renamed.c
	$(CC) $(CFLAGS) -o $@ $^ -Ipda

run: re pda

re: bench_re
	@echo ""
	@echo "=== RE Benchmark: SNOBOL4-tiny vs PCRE2 JIT ==="
	./bench_re

pda: bench_pda
	@echo ""
	@echo "=== PDA Benchmark: SNOBOL4-tiny vs Bison LALR(1) ==="
	./bench_pda

clean:
	rm -f bench_re bench_pda
	rm -f pda/anbn.tab.c pda/anbn.tab.h pda/anbn_renamed.c
	rm -f pda/dyck.tab.c pda/dyck.tab.h pda/dyck_renamed.c
