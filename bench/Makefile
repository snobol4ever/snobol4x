# SNOBOL4-tiny Benchmark Suite
# Run: make run
# Requires: libpcre2-dev, bison, python3

CC     = cc
CFLAGS = -O2
PCRE2  = $(shell pcre2-config --libs8)

.PHONY: all run re pda clean

all: re_bench pda_bench

re_bench: bench_re_vs_tiny.c
	$(CC) $(CFLAGS) -o $@ $< $(PCRE2)

pda/anbn_renamed.c: pda/anbn.y
	bison -d -o pda/anbn.tab.c pda/anbn.y
	python3 rename_bison.py pda/anbn.tab.c anbn pda/anbn_renamed.c

pda/dyck_renamed.c: pda/dyck.y
	bison -d -o pda/dyck.tab.c pda/dyck.y
	python3 rename_bison.py pda/dyck.tab.c dyck pda/dyck_renamed.c

pda_bench: pda/bench_pda.c pda/anbn_renamed.c pda/dyck_renamed.c
	$(CC) $(CFLAGS) -o $@ $^ -Ipda

run: re pda

re: re_bench
	@echo ""
	@echo "=== RE Benchmark: SNOBOL4-tiny vs PCRE2 JIT ==="
	./re_bench

pda: pda_bench
	@echo ""
	@echo "=== PDA Benchmark: SNOBOL4-tiny vs Bison LALR(1) ==="
	./pda_bench

clean:
	rm -f re_bench pda_bench
	rm -f pda/anbn.tab.c pda/anbn.tab.h pda/anbn_renamed.c
	rm -f pda/dyck.tab.c pda/dyck.tab.h pda/dyck_renamed.c
