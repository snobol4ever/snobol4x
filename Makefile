# Makefile — one4all unified build
#
# Primary targets:
#   make scrip        — build the unified scrip x86 executable
#   make all          — alias for scrip
#   make setup        — install system packages + CSNOBOL4 + SPITBOL oracle
#   make test         — run corpus (--sm-run, PASS=178 gate)
#   make test-ir      — run corpus (--ir-run mode)
#   make test-all     — both passes back-to-back
#   make monitor-ipc  — build test/monitor/monitor_ipc.so
#   make clean        — remove build artefacts
#   make distclean    — clean + remove /tmp caches
#
# Runner wrappers (run a single .sno file):
#   make run SNO=file.sno              — default (--sm-run)
#   make run-ir SNO=file.sno           — --ir-run (IR tree-walk)
#   make run-jvm SNO=file.sno          — legacy JVM (until M-JITEM-JVM)
#   make run-net SNO=file.sno          — legacy .NET (until M-JITEM-NET)
#
# Note: run-asm retired — replaced by: scrip --jit-emit --x64 (M-JITEM-X64)
#
# Prerequisites:
#   apt-get install -y libgc-dev flex nasm build-essential libgmp-dev m4
#
# Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6

ROOT    := $(shell pwd)
SRC     := $(ROOT)/src
RT      := $(SRC)/runtime
BOXES   := $(RT)/x86
CORPUS  ?= $(ROOT)/../corpus
OBJ     := /tmp/si_objs
CC      := gcc
WARN    := -w
CBASE   := -O0 -g $(WARN) -I$(SRC) -I$(RT)/x86 -I$(RT) -I$(RT)/x86
CRT     := $(CBASE) -I$(RT)/x86 -DDYN_ENGINE_LINKED
LIBS    := -lgc -lm

# Runner defaults
SNO          ?= $(error SNO is required — e.g. make run SNO=prog.sno)
INC          ?= $(CORPUS)/programs/inc
JVM_CACHE    := /tmp/scrip_jvm_cache
NET_CACHE    := /tmp/scrip_net_cache
JASMIN       := $(SRC)/backend/jasmin.jar
SCRIP_CC_BIN := $(ROOT)/scrip

.PHONY: all scrip scrip-interp scrip setup \
        test test-ir test-all \
        monitor-ipc \
        run run-ir run-jvm run-net \
        clean distclean

# ── Primary target ────────────────────────────────────────────────────────────

all: scrip

# ── scrip — unified driver (all modes, all frontends) ────────────────────────
# WASM removed from scrip build (2026-04-08): --jit-emit --wasm / emit_wasm.c
# dropped. Use scrip legacy driver if WASM emission is ever needed.

scrip:
	@mkdir -p $(OBJ)
	@rm -f $(OBJ)/*.o
	$(CC) $(CBASE) -c $(SRC)/frontend/snobol4/snobol4.lex.c -o $(OBJ)/snobol4.lex.o
	$(CC) $(CBASE) -c $(SRC)/frontend/snobol4/snobol4.tab.c -o $(OBJ)/snobol4.tab.o
	$(CC) $(CRT)   -c $(RT)/x86/snobol4.c               -o $(OBJ)/snobol4.o
	$(CC) $(CRT)   -c $(RT)/x86/snobol4_pattern.c        -o $(OBJ)/snobol4_pattern.o
	$(CC) $(CRT)   -c $(RT)/x86/snobol4_invoke.c                 -o $(OBJ)/snobol4_invoke.o
	$(CC) $(CRT)   -c $(RT)/x86/snobol4_argval.c                 -o $(OBJ)/snobol4_argval.o
	$(CC) $(CRT)   -c $(RT)/x86/snobol4_nmd.c                    -o $(OBJ)/snobol4_nmd.o
	$(CC) $(CRT)   -c $(RT)/x86/stmt_exec.c                  -o $(OBJ)/stmt_exec.o
	$(CC) $(CRT)   -c $(RT)/x86/eval_code.c                  -o $(OBJ)/eval_code.o
	$(CC) $(CRT)   -c $(RT)/x86/engine.c                  -o $(OBJ)/engine.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_pool.c                    -o $(OBJ)/bb_pool.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_emit.c                    -o $(OBJ)/bb_emit.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_build.c               -o $(OBJ)/bb_build.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_flat.c                    -o $(OBJ)/bb_flat.o
	$(CC) $(CRT) -c $(RT)/x86/bb_boxes.c -o $(OBJ)/bb_boxes.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -DIR_DEFINE_NAMES \
	    -c $(SRC)/ir/ir_print.c -o $(OBJ)/ir_print.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/snocone/snocone_lex.c    -o $(OBJ)/snocone_lex.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/snocone/snocone_parse.c  -o $(OBJ)/snocone_parse.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/snocone/snocone_lower.c  -o $(OBJ)/snocone_lower.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/snocone/snocone_cf.c     -o $(OBJ)/snocone_cf.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/snocone/snocone_driver.c -o $(OBJ)/snocone_driver.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_lex.c      -o $(OBJ)/prolog_lex.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_parse.c    -o $(OBJ)/prolog_parse.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_lower.c    -o $(OBJ)/prolog_lower.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_atom.c     -o $(OBJ)/prolog_atom.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_builtin.c  -o $(OBJ)/prolog_builtin.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_unify.c    -o $(OBJ)/prolog_unify.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/prolog_driver.c   -o $(OBJ)/prolog_driver.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/prolog/pl_broker.c       -o $(OBJ)/pl_broker.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_lex.c         -o $(OBJ)/icon_lex.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_ast.c         -o $(OBJ)/icon_ast.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_parse.c       -o $(OBJ)/icon_parse.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_lower.c       -o $(OBJ)/icon_lower.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_runtime.c     -o $(OBJ)/icon_runtime.o

	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_driver.c      -o $(OBJ)/icon_driver.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_prog.c    -o $(OBJ)/sm_prog.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_interp.c  -o $(OBJ)/sm_interp.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_lower.c   -o $(OBJ)/sm_lower.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_image.c   -o $(OBJ)/sm_image.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_codegen.c -o $(OBJ)/sm_codegen.o
	$(CC) $(CRT)   -c $(SRC)/driver/scrip.c  -o $(OBJ)/scrip_driver.o
	$(CC) $(OBJ)/*.o $(LIBS) -o scrip
	@echo "Built: scrip"

# backward-compat symlink
scrip-interp: scrip
	@ln -sf scrip scrip-interp

# ── monitor_ipc.so ────────────────────────────────────────────────────────────

monitor-ipc:
	gcc -shared -fPIC \
	    -o test/monitor/monitor_ipc.so \
	    test/monitor/monitor_ipc.c
	@echo "Built: test/monitor/monitor_ipc.so"

# ── Environment setup (idempotent) ────────────────────────────────────────────

setup:
	bash $(ROOT)/setup.sh

# ── Test targets ──────────────────────────────────────────────────────────────

test: scrip
	CORPUS=$(CORPUS) bash test/run_interp_broad.sh

test-ir: scrip
	INTERP="./scrip --ir-run" CORPUS=$(CORPUS) bash test/run_interp_broad.sh

test-all: test test-ir

# ── Runner wrappers ───────────────────────────────────────────────────────────

run: scrip
	./scrip $(SNO)

run-ir: scrip
	./scrip --ir-run $(SNO)

# Legacy JVM runner — uses old scrip text emitter until M-JITEM-JVM lands
run-jvm: scrip
	@mkdir -p $(JVM_CACHE); \
	base=$$(basename $(SNO) .sno); \
	hash=$$(echo $(SNO) | md5sum | cut -c1-8); \
	key=$${base}_$${hash}; \
	jfile=$(JVM_CACHE)/$${key}.j; \
	stamp=$(JVM_CACHE)/$${key}.stamp; \
	$(SCRIP_CC_BIN) -jvm $(SNO) > $$jfile; \
	classname=$$(grep '\.class' $$jfile | head -1 | awk '{print $$NF}'); \
	j_md5=$$(md5sum $$jfile | cut -d' ' -f1); \
	cached_md5=$$(cat $$stamp 2>/dev/null || echo ''); \
	if [ "$$j_md5" != "$$cached_md5" ] || [ ! -f $(JVM_CACHE)/$$classname.class ]; then \
	    java -jar $(JASMIN) $$jfile -d $(JVM_CACHE) >/dev/null; \
	    echo "$$j_md5" > $$stamp; \
	fi; \
	java -cp $(JVM_CACHE) $$classname

# Legacy .NET runner — uses old scrip text emitter until M-JITEM-NET lands
run-net: scrip
	@mkdir -p $(NET_CACHE); \
	base=$$(basename $(SNO) .sno); \
	hash=$$(echo $(SNO) | md5sum | cut -c1-8); \
	key=$${base}_$${hash}; \
	il=$(NET_CACHE)/$${key}.il; \
	exe=$(NET_CACHE)/$${key}.exe; \
	stamp=$(NET_CACHE)/$${key}.stamp; \
	$(SCRIP_CC_BIN) -net $(SNO) > $$il; \
	il_md5=$$(md5sum $$il | cut -d' ' -f1); \
	cached_md5=$$(cat $$stamp 2>/dev/null || echo ''); \
	if [ "$$il_md5" != "$$cached_md5" ] || [ ! -f $$exe ]; then \
	    ilasm $$il /output:$$exe >/dev/null; \
	    echo "$$il_md5" > $$stamp; \
	fi; \
	mono $$exe

# ── Clean ─────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(OBJ) scrip scrip-interp

distclean: clean
	rm -rf $(JVM_CACHE) $(NET_CACHE) /tmp/snobol4_asm_* /tmp/scrip_cc_*
