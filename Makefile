# Makefile — scrip (unified: --interp / --gen)
# Usage: make | make scrip | make test | make clean
# Prerequisites: apt-get install -y libgc-dev flex
# Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6

ROOT    := $(shell pwd)
SRC     := $(ROOT)/src
RT      := $(SRC)/runtime
BOXES   := $(RT)/boxes
CORPUS  ?= /home/claude/corpus
OBJ     := /tmp/si_objs
CC      := gcc
WARN    := -Wno-unused-function -Wno-unused-variable -Wno-incompatible-pointer-types
CBASE   := -O0 -g $(WARN) -I$(SRC) -I$(RT)/snobol4 -I$(RT) -I$(BOXES)/shared
CRT     := $(CBASE) -I$(RT)/dyn -DDYN_ENGINE_LINKED
LIBS    := -lgc -lm

.PHONY: all scrip scrip-interp test clean

all: scrip

scrip:
	@mkdir -p $(OBJ)
	@rm -f $(OBJ)/*.o
	$(CC) $(CBASE) -c $(SRC)/frontend/snobol4/snobol4.lex.c -o $(OBJ)/snobol4.lex.o
	$(CC) $(CBASE) -c $(SRC)/frontend/snobol4/snobol4.tab.c -o $(OBJ)/snobol4.tab.o
	$(CC) $(CRT)   -c $(RT)/snobol4/snobol4.c               -o $(OBJ)/snobol4.o
	$(CC) $(CRT)   -c $(RT)/snobol4/snobol4_pattern.c        -o $(OBJ)/snobol4_pattern.o
	$(CC) $(CRT)   -c $(RT)/snobol4/invoke.c                 -o $(OBJ)/invoke.o
	$(CC) $(CRT)   -c $(RT)/snobol4/argval.c                 -o $(OBJ)/argval.o
	$(CC) $(CRT)   -c $(RT)/snobol4/nmd.c                    -o $(OBJ)/nmd.o
	$(CC) $(CRT)   -c $(RT)/dyn/stmt_exec.c                  -o $(OBJ)/stmt_exec.o
	$(CC) $(CRT)   -c $(RT)/dyn/eval_code.c                  -o $(OBJ)/eval_code.o
	$(CC) $(CRT)   -c $(RT)/asm/bb_pool.c                    -o $(OBJ)/bb_pool.o
	$(CC) $(CRT)   -c $(RT)/asm/bb_emit.c                    -o $(OBJ)/bb_emit.o
	$(CC) $(CRT)   -c $(RT)/asm/bb_build_bin.c               -o $(OBJ)/bb_build_bin.o
	$(CC) $(CRT)   -c $(RT)/asm/bb_flat.c                    -o $(OBJ)/bb_flat.o
	$(CC) $(CRT)   -c $(RT)/asm/x86_stubs_interp.c           -o $(OBJ)/x86_stubs_interp.o
	$(CC) $(CRT)   -c $(RT)/engine/engine.c                  -o $(OBJ)/engine.o
	@for f in $$(find $(RT)/boxes -name 'bb_*.c' | grep -v 'bb_dvar\|bb_capture'); do \
	    b=$$(basename $$f .c); \
	    $(CC) $(CBASE) -c $$f -o $(OBJ)/$$b.o; \
	done
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -DIR_DEFINE_NAMES -c $(SRC)/ir/ir_print.c  -o $(OBJ)/ir_print.o
	$(CC) $(CRT)   -c $(SRC)/driver/scrip.c                  -o $(OBJ)/scrip_driver.o
	$(CC) $(OBJ)/*.o $(LIBS) -o scrip
	@echo "Built: scrip"

# backward-compat alias — old harness invocations using scrip-interp still work
scrip-interp: scrip
	@ln -sf scrip scrip-interp

test: scrip
	CORPUS=$(CORPUS) bash test/run_interp_broad.sh

clean:
	rm -rf $(OBJ) scrip scrip-interp
