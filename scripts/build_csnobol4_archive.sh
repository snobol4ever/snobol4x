#!/usr/bin/env bash
# build_csnobol4_archive.sh — compile CSNOBOL4 as a linkable archive for IM-15b.
# Produces /home/claude/csnobol4/libcsnobol4.a with main renamed to csnobol4_main.
# All objects compiled with -fPIC. Idempotent.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CSN_REPO="/home/claude/csnobol4"
OUT="$CSN_REPO/libcsnobol4.a"
[ -d "$CSN_REPO/.git" ] || { echo "FAIL  csnobol4 repo not found at $CSN_REPO"; exit 1; }
if [ -f "$OUT" ] && [ "$OUT" -nt "$CSN_REPO/isnobol4.c" ] && [ "$OUT" -nt "$CSN_REPO/main.c" ]; then
    echo "SKIP  libcsnobol4.a already up to date"; echo "OK    archive: $OUT"; exit 0
fi
echo "BUILD libcsnobol4.a ..."
cd "$CSN_REPO"
CFLAGS_BASE="-Wall -O3 -Iinclude -I. -DHAVE_CONFIG_H -DSNOBOL4 -fPIC -Dmain=csnobol4_main"
CFLAGS_SNO="$CFLAGS_BASE -Wno-return-type -Wno-switch"
compile() { local src="$1" obj="$2" flags="${3:-$CFLAGS_BASE}"; cc $flags -c "$src" -o "$obj"; }
compile isnobol4.c                   isnobol4_pic.o   "$CFLAGS_SNO"
compile main.c                       main_pic.o
compile data.c                       data_pic.o
compile data_init.c                  data_init_pic.o
compile syn.c                        syn_pic.o
compile lib/bal.c                    bal_pic.o
compile lib/break.c                  break_pic.o
compile lib/date.c                   date_pic.o
compile lib/dump.c                   dump_pic.o
compile lib/posix/dynamic.c          dynamic_pic.o
compile lib/endex.c                  endex_pic.o
compile lib/generic/expops.c         expops_pic.o
compile lib/generic/fisatty.c        fisatty_pic.o
compile lib/snolib/getstring.c       getstring_pic.o
compile lib/snolib/handle.c          handle_pic.o
compile lib/hash.c                   hash_pic.o
compile lib/bsd/inet6.c              inet6_pic.o
compile lib/init.c                   init_pic.o
compile lib/generic/intspc.c         intspc_pic.o
compile lib/io.c                     io_pic.o
compile lib/lexcmp.c                 lexcmp_pic.o
compile lib/unix98/load.c            load_pic.o
compile lib/loadx.c                  loadx_pic.o
compile lib/bsd/mstime.c             mstime_pic.o
compile lib/generic/newer.c          newer_pic.o
compile lib/ordvst.c                 ordvst_pic.o
compile lib/pair.c                   pair_pic.o
compile lib/pat.c                    pat_pic.o
compile lib/pml.c                    pml_pic.o
compile lib/bsd/ptyio_obj.c          ptyio_obj_pic.o
compile lib/realst.c                 realst_pic.o
compile lib/replace.c                replace_pic.o
compile lib/snolib/retstring.c       retstring_pic.o
compile lib/ansi/spcint.c            spcint_pic.o
compile lib/ansi/spreal.c            spreal_pic.o
compile lib/stdio_obj.c              stdio_obj_pic.o
compile lib/str.c                    str_pic.o
compile lib/stream.c                 stream_pic.o
compile lib/posix/suspend.c          suspend_pic.o
compile lib/posix/term.c             term_pic.o
compile lib/top.c                    top_pic.o
compile lib/tree.c                   tree_pic.o
compile lib/posix/tty.c              tty_pic.o
compile lib/snolib/atan.c            atan_pic.o
compile lib/snolib/chop.c            chop_pic.o
compile lib/snolib/cos.c             cos_pic.o
compile lib/snolib/delete.c          delete_pic.o
compile lib/generic/exists.c         exists_pic.o
compile lib/snolib/exit.c            exit_pic.o
compile lib/generic/execute.c        execute_pic.o
compile lib/snolib/exp.c             exp_pic.o
compile lib/snolib/file.c            file_pic.o
compile lib/snolib/findunit.c        findunit_pic.o
compile lib/snolib/host.c            host_pic.o
compile lib/snolib/log.c             log_pic.o
compile lib/snolib/ord.c             ord_pic.o
compile lib/snolib/rename.c          rename_pic.o
compile lib/snolib/serv.c            serv_pic.o
compile lib/snolib/sin.c             sin_pic.o
compile lib/snolib/sqrt.c            sqrt_pic.o
compile lib/snolib/sset.c            sset_pic.o
compile lib/posix/sys.c              sys_pic.o
compile lib/snolib/tan.c             tan_pic.o
compile lib/auxil/bindresvport.c     bindresvport_pic.o
compile lib/bsd/popen.c              popen_pic.o
compile lib/auxil/bufio_obj.c        bufio_obj_pic.o
compile lib/compio_obj.c             compio_obj_pic.o
compile lib/posix/memio_obj.c        memio_obj_pic.o
ar rcs "$OUT" \
    isnobol4_pic.o main_pic.o data_pic.o data_init_pic.o syn_pic.o \
    bal_pic.o break_pic.o date_pic.o dump_pic.o dynamic_pic.o endex_pic.o \
    expops_pic.o fisatty_pic.o getstring_pic.o handle_pic.o hash_pic.o \
    inet6_pic.o init_pic.o intspc_pic.o io_pic.o loadx_pic.o lexcmp_pic.o \
    load_pic.o mstime_pic.o newer_pic.o ordvst_pic.o pair_pic.o pat_pic.o \
    pml_pic.o ptyio_obj_pic.o realst_pic.o replace_pic.o retstring_pic.o \
    spcint_pic.o spreal_pic.o stdio_obj_pic.o str_pic.o stream_pic.o \
    suspend_pic.o term_pic.o top_pic.o tree_pic.o tty_pic.o \
    atan_pic.o chop_pic.o cos_pic.o delete_pic.o exists_pic.o exit_pic.o \
    execute_pic.o exp_pic.o file_pic.o findunit_pic.o host_pic.o log_pic.o \
    ord_pic.o rename_pic.o serv_pic.o sin_pic.o sqrt_pic.o sset_pic.o \
    sys_pic.o tan_pic.o bindresvport_pic.o popen_pic.o bufio_obj_pic.o \
    compio_obj_pic.o memio_obj_pic.o
[ -f "$OUT" ] || { echo "FAIL  ar did not produce $OUT"; exit 1; }
echo "OK    libcsnobol4.a built ($(du -sh "$OUT" | cut -f1))"
echo "OK    archive: $OUT"
