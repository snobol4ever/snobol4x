# Makefile — one4all unified build
#
# Major targets:
#   make all          — build scrip + scrip-cc
#   make scrip        — interpreter/hybrid (--interp / --hybrid / --gen modes)
#   make scrip-cc     — compiler driver (ASM / JVM / .NET / JS / WASM backends)
#   make setup        — install system packages + CSNOBOL4 + SPITBOL oracle
#   make test         — run_interp_broad (--interp mode, PASS=178 gate)
#   make test-hybrid  — run_interp_broad (--hybrid mode)
#   make test-all     — both test passes back-to-back
#   make monitor-ipc  — build test/monitor/monitor_ipc.so
#   make clean        — remove build artefacts
#   make distclean    — clean + remove /tmp caches
#
# Backend runner wrappers (compile+run a single .sno file):
#   make run-asm SNO=file.sno   — ASM backend (nasm + link)
#   make run-jvm SNO=file.sno   — JVM backend (jasmin + java)
#   make run-net SNO=file.sno   — .NET backend (ilasm + mono)
#
# Prerequisites:
#   apt-get install -y libgc-dev flex nasm build-essential libgmp-dev m4
#
# Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6

ROOT    := $(shell pwd)
SRC     := $(ROOT)/src
RT      := $(SRC)/runtime
BOXES   := $(RT)/boxes
CORPUS  ?= $(ROOT)/../corpus
OBJ     := /tmp/si_objs
CC      := gcc
WARN    := -w
CBASE   := -O0 -g $(WARN) -I$(SRC) -I$(RT)/snobol4 -I$(RT) -I$(BOXES)/shared
CRT     := $(CBASE) -I$(RT)/dyn -DDYN_ENGINE_LINKED
LIBS    := -lgc -lm

# Backend runner defaults
SNO          ?= $(error SNO is required — e.g. make run-asm SNO=prog.sno)
INC          ?= $(CORPUS)/programs/inc
SCRIP_CC_BIN := $(ROOT)/scrip-cc
JVM_CACHE    := /tmp/scrip_cc_jvm_cache
NET_CACHE    := /tmp/scrip_cc_net_cache
JASMIN       := $(SRC)/backend/jasmin.jar
RUNTIME_NET  := $(RT)/net

.PHONY: all scrip scrip-cc scrip-interp setup \
        test test-hybrid test-all \
        monitor-ipc \
        run-asm run-jvm run-net \
        clean distclean

# ── Primary targets ───────────────────────────────────────────────────────────

all: scrip

# ── scrip — unified driver (all modes, all frontends) ────────────────────────

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
	$(CC) $(CBASE) -I$(SRC)/frontend/snobol4 -DIR_DEFINE_NAMES \
	    -c $(SRC)/ir/ir_print.c -o $(OBJ)/ir_print.o
	$(CC) $(CRT)   -c $(RT)/sm/sm_prog.c    -o $(OBJ)/sm_prog.o
	$(CC) $(CRT)   -c $(RT)/sm/sm_interp.c  -o $(OBJ)/sm_interp.o
	$(CC) $(CRT)   -c $(RT)/sm/sm_lower.c   -o $(OBJ)/sm_lower.o
	$(CC) $(CRT)   -c $(SRC)/driver/scrip.c  -o $(OBJ)/scrip_driver.o
	$(CC) $(OBJ)/*.o $(LIBS) -o scrip
	@echo "Built: scrip"

# backward-compat alias
scrip-interp: scrip
	@ln -sf scrip scrip-interp

# ── scrip-cc (compiler driver — all backends) ─────────────────────────────────

scrip-cc:
	$(MAKE) -C $(SRC)
	@echo "Built: scrip-cc"

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

test-hybrid: scrip
	INTERP="./scrip --hybrid" CORPUS=$(CORPUS) bash test/run_interp_broad.sh

test-all: test test-hybrid

# ── Backend runners (compile + run a single .sno file) ────────────────────────

run-asm: scrip-cc
	@WORK=$$(mktemp -d /tmp/snobol4_asm_XXXXXX); \
	trap 'rm -rf "$$WORK"' EXIT; \
	gcc -O0 -g -c $(RT)/asm/snobol4_stmt_rt.c    -I$(RT)/snobol4 -I$(RT) -I$(SRC)/frontend/snobol4 -w -o $$WORK/stmt_rt.o; \
	gcc -O0 -g -c $(RT)/snobol4/snobol4.c         -I$(RT)/snobol4 -I$(RT) -I$(SRC)/frontend/snobol4 -w -o $$WORK/snobol4.o; \
	gcc -O0 -g -c $(RT)/mock/mock_includes.c       -I$(RT)/snobol4 -I$(RT) -I$(SRC)/frontend/snobol4 -w -o $$WORK/mock.o; \
	gcc -O0 -g -c $(RT)/snobol4/snobol4_pattern.c -I$(RT)/snobol4 -I$(RT) -I$(SRC)/frontend/snobol4 -w -o $$WORK/pat.o; \
	gcc -O0 -g -c $(RT)/engine/engine.c            -I$(RT)/snobol4 -I$(RT) -I$(SRC)/frontend/snobol4 -w -o $$WORK/eng.o; \
	gcc -O0 -g -c $(RT)/asm/blk_alloc.c            -I$(RT)/asm -w -o $$WORK/blk_alloc.o; \
	gcc -O0 -g -c $(RT)/asm/blk_reloc.c            -I$(RT)/asm -w -o $$WORK/blk_reloc.o; \
	$(SCRIP_CC_BIN) -asm -I$(INC) $(SNO) > $$WORK/prog.s; \
	nasm -f elf64 -I$(RT)/asm/ $$WORK/prog.s -o $$WORK/prog.o; \
	gcc -no-pie $$WORK/prog.o $$WORK/stmt_rt.o $$WORK/snobol4.o $$WORK/mock.o \
	    $$WORK/pat.o $$WORK/eng.o $$WORK/blk_alloc.o $$WORK/blk_reloc.o \
	    -lgc -lm -o $$WORK/prog; \
	$$WORK/prog

run-jvm: scrip-cc
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

run-net: scrip-cc
	@mkdir -p $(NET_CACHE); \
	for dll in snobol4lib.dll snobol4run.dll; do \
	    src=$(RUNTIME_NET)/$$dll; dst=$(NET_CACHE)/$$dll; \
	    [ -f $$src ] && { [ ! -f $$dst ] || ! diff -q $$src $$dst >/dev/null 2>&1; } && cp $$src $$dst || true; \
	done; \
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
	$(MAKE) -C $(SRC) clean

distclean: clean
	rm -rf $(JVM_CACHE) $(NET_CACHE) /tmp/snobol4_asm_* /tmp/scrip_cc_*
