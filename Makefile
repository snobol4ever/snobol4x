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
	$(CC) $(CRT)   -c $(RT)/x86/bb_pool.c                    -o $(OBJ)/bb_pool.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_emit.c                    -o $(OBJ)/bb_emit.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_build.c               -o $(OBJ)/bb_build.o
	$(CC) $(CRT)   -c $(RT)/x86/bb_flat.c                    -o $(OBJ)/bb_flat.o
	$(CC) $(CRT) -c $(RT)/x86/bb_boxes.c -o $(OBJ)/bb_boxes.o
	$(CC) $(CRT) -c $(RT)/x86/bb_broker.c -o $(OBJ)/bb_broker.o
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
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_parse.c       -o $(OBJ)/icon_parse.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_runtime.c     -o $(OBJ)/icon_runtime.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_gen.c         -o $(OBJ)/icon_gen.o

	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/icon/icon_driver.c      -o $(OBJ)/icon_driver.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -I$(SRC)/frontend/raku -c $(SRC)/frontend/raku/raku.tab.c    -o $(OBJ)/raku.tab.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -I$(SRC)/frontend/raku -c $(SRC)/frontend/raku/raku.lex.c    -o $(OBJ)/raku.lex.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -I$(SRC)/frontend/raku -c $(SRC)/frontend/raku/raku_driver.c -o $(OBJ)/raku_driver.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -I$(SRC)/frontend/raku -c $(SRC)/frontend/raku/raku_re.c      -o $(OBJ)/raku_re.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/rebus/rebus.tab.c    -o $(OBJ)/rebus.tab.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/rebus/lex.rebus.c    -o $(OBJ)/lex.rebus.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/rebus/rebus_lower.c  -o $(OBJ)/rebus_lower.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/rebus/rebus_emit.c   -o $(OBJ)/rebus_emit.o
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -c $(SRC)/frontend/rebus/rebus_print.c  -o $(OBJ)/rebus_print.o
	$(CC) $(CRT)   -c $(SRC)/runtime/interp/icn_runtime.c -o $(OBJ)/icn_runtime.o
	$(CC) $(CRT)   -c $(SRC)/runtime/interp/pl_runtime.c  -o $(OBJ)/pl_runtime.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_prog.c    -o $(OBJ)/sm_prog.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_interp.c  -o $(OBJ)/sm_interp.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_lower.c   -o $(OBJ)/sm_lower.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_image.c   -o $(OBJ)/sm_image.o
	$(CC) $(CRT)   -c $(RT)/x86/sm_codegen.c -o $(OBJ)/sm_codegen.o
	$(CC) $(CRT)   -c $(SRC)/driver/interp.c  -o $(OBJ)/interp.o
	$(CC) $(CRT)   -c $(SRC)/driver/sync_monitor.c -o $(OBJ)/sync_monitor.o
	$(CC) $(CRT)   -c $(SRC)/driver/polyglot.c -o $(OBJ)/polyglot.o
	$(CC) $(CRT)   -c $(SRC)/driver/scrip.c  -o $(OBJ)/scrip_driver.o
	$(CC) -m64 -no-pie $(OBJ)/*.o $(LIBS) -o scrip
	@echo "Built: scrip"

# backward-compat symlink
scrip-interp: scrip
	@ln -sf scrip scrip-interp

# ── scrip-monitor: scrip with CSNOBOL4 4th executor linked in (IM-15b) ───────
# Build: make scrip-monitor CSN_A=/home/claude/csnobol4/libcsnobol4.a
# Requires: bash scripts/build_csnobol4_archive.sh first
CSN_A   ?= /home/claude/csnobol4/libcsnobol4.a
CSN_INC ?= /home/claude/csnobol4

scrip-monitor:
	@# Build all scrip objects, then relink with CSNOBOL4 4th executor
	$(MAKE) -f Makefile scrip
	$(CC) $(CRT) -DWITH_CSNOBOL4=1 -I$(CSN_INC) \
	      -c $(SRC)/driver/csnobol4_shim.c -o $(OBJ)/csnobol4_shim_csn.o
	$(CC) $(CRT) -DWITH_CSNOBOL4=1 \
	      -c $(SRC)/driver/sync_monitor.c -o $(OBJ)/sync_monitor_csn.o
	$(CC) -m64 -no-pie -Wl,--allow-multiple-definition \
	      $(OBJ)/csnobol4_shim_csn.o $(OBJ)/sync_monitor_csn.o \
	      $(filter-out $(OBJ)/sync_monitor.o $(OBJ)/sync_monitor_csn.o $(OBJ)/csnobol4_shim.o $(OBJ)/csnobol4_shim_csn.o $(OBJ)/scrip_driver.o, $(wildcard $(OBJ)/*.o)) \
	      $(OBJ)/scrip_driver.o \
	      $(CSN_A) $(LIBS) -lutil -ldl -lz -lbz2 -o scrip-monitor
	@echo "Built: scrip-monitor (with CSNOBOL4 4th executor)"

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
