/*
 * sil_data.h — declarations for all named globals from v311.sil §23 Data
 *
 * Faithful C translation of v311.sil §23 "Data" (lines 10481–12293).
 * Every named DESCR, SPEC, STRING, FORMAT, REAL, and ARRAY from that
 * section is declared here with its exact SIL name.
 *
 * Naming:
 *   SIL "X DESCR ..."  →  DESCR_t X;
 *   SIL "X STRING 's'" →  const char X[];
 *   SIL "X FORMAT 's'" →  const char X[];
 *   SIL "X REAL   n"   →  real_t X;
 *   SIL "X ARRAY  n"   →  DESCR_t X[n];   (static arena arrays)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M1
 */

#ifndef SIL_DATA_H
#define SIL_DATA_H

#include "sil_types.h"

/* ── Scratch DESCRs ──────────────────────────────────────────────────── */
/* General-purpose working registers — named in v311.sil data section   */

extern DESCR_t XPTR, YPTR, ZPTR, WPTR, TPTR;        /* pointer scratch   */
extern DESCR_t XCL,  YCL,  ZCL,  WCL,  TCL;        /* count/value scratch */
extern DESCR_t TMVAL, SCL, AXPTR, DTCL;              /* misc scratch       */
extern DESCR_t XSPPTR, YSPPTR;                       /* spec-ptr scratch   */

/* ── Scratch SPECs ───────────────────────────────────────────────────── */

extern SPEC_t XSP,  YSP,  ZSP,  WSP,  TSP;
extern SPEC_t TXSP, TEXTSP, NEXTSP, HEADSP, LNBFSP;
extern SPEC_t SPECR1, SPECR2;                        /* conversion scratch */
extern SPEC_t DPSP;                                  /* DTREP output spec  */

/* ── Interpreter registers ───────────────────────────────────────────── */

extern DESCR_t OCBSCL;    /* object code block base                       */
extern DESCR_t OCICL;     /* object code index (current offset)           */
extern DESCR_t OCBSVC;    /* saved object code base (EXPVAL)              */
extern DESCR_t CMBSCL;    /* compiler output block base                   */
extern DESCR_t CMOFCL;    /* compiler output offset                       */
extern DESCR_t PATBCL;    /* pattern code block base                      */
extern DESCR_t PATICL;    /* pattern code index                           */
extern DESCR_t FRTNCL;    /* failure return code offset                   */
extern DESCR_t OCSVCL;    /* saved object code pointer (compiler)         */

/* ── Pattern/Naming stacks ───────────────────────────────────────────── */

extern DESCR_t PDLPTR;    /* pattern history list pointer                 */
extern DESCR_t PDLHED;    /* pattern history list head (save point)       */
extern DESCR_t PDLEND;    /* pattern history list end                     */
extern DESCR_t NAMICL;    /* naming list current index                    */
extern DESCR_t NHEDCL;    /* naming list head (save point)                */
extern DESCR_t SCBSCL;    /* string buffer base                           */
extern DESCR_t NBSPTR;    /* name buffer specifier pointer                */

/* ── System stack ────────────────────────────────────────────────────── */

extern DESCR_t STKHED;    /* interpreter stack (allocated in arena_init)  */
extern DESCR_t STKEND;    /* interpreter stack end                        */
extern DESCR_t STKPTR;    /* interpreter stack current pointer            */

/* ── Arena / storage management ──────────────────────────────────────── */

extern DESCR_t FRSGPT;    /* free storage pointer (next alloc)            */
extern DESCR_t TLSGP1;    /* top of free storage (GC limit)               */
extern DESCR_t HDSGPT;    /* heap start (for GC)                          */
extern DESCR_t MVSGPT;    /* compression barrier (GC)                     */
extern DESCR_t BKLTCL;    /* block size for GC request                    */

/* ── Switches ────────────────────────────────────────────────────────── */

extern DESCR_t ALCL;      /* ARG(F,N) entry point switch                  */
extern DESCR_t ARRMRK;    /* ARRAY prototype end switch                   */
extern DESCR_t BANRCL;    /* display startup banner (init=1)              */
extern DESCR_t COMPCL;    /* compiling; list syntax errors (init=1)       */
extern DESCR_t CUTNO;     /* [E3.2.2]                                      */
extern DESCR_t CNSLCL;    /* label redefinition switch                    */
extern DESCR_t DATACL;    /* DATA prototype end switch                    */
extern DESCR_t EXECCL;    /* EXECUTE switch (init=1)                      */
extern DESCR_t FNVLCL;    /* FUNCTION-VALUE switch for trace              */
extern DESCR_t HIDECL;    /* hide statement numbers [PLB102]              */
extern DESCR_t INICOM;    /* initialization switch [E3.10.6]              */
extern DESCR_t MAXLEN;    /* maximum length for pattern matching          */
extern DESCR_t LENFCL;    /* length failure switch                        */
extern DESCR_t LISTCL;    /* compiler listing switch                      */
extern DESCR_t LLIST;     /* left listing switch (init=1) [PLB12]         */
extern DESCR_t NAMGCL;    /* naming switch for SJSR                       */
extern DESCR_t NERRCL;    /* NOERROR switch (init=1) [PLB9]               */
extern DESCR_t SCERCL;    /* error branch switch                          */
extern DESCR_t SPITCL;    /* SPITBOL features (init=1) [PLB32][PLB33]     */
extern DESCR_t STATCL;    /* display statistics [PLB12][PLB33]            */
extern DESCR_t INSW;      /* &INPUT (init=1)                              */
extern DESCR_t OUTSW;     /* &OUTPUT (init=1)                             */
extern DESCR_t FULLCL;    /* &FULLSCAN                                    */
extern DESCR_t CONVSW;    /* GENVAR conversion switch                     */

/* ── Constants (DESCR-valued) ───────────────────────────────────────── */

extern DESCR_t ARBSIZ;    /* node size for ARBNO(P) = 8*NODESZ            */
extern DESCR_t CHARCL;    /* constant 1 (character length)                */
extern DESCR_t CNDSIZ;    /* compiler node size = CNODSZS                 */
extern DESCR_t CODELT;    /* object code excess = 200*DESCR               */
extern DESCR_t DSCRTW;    /* constant 2*DESCR                             */
extern DESCR_t EOSCL;     /* end-of-statement switch = EOSTYP             */
extern DESCR_t ESALIM;    /* bound on compilation errors = ESASIZ*DESCR   */
extern DESCR_t EXTVSL;    /* [V3.11] = EXTSIZ*2*DESCR                     */
extern DESCR_t FBLKRQ;    /* function block quantum = FBKLSZ              */
extern DESCR_t GOBRCL;    /* goto break character switch                  */
extern DESCR_t GTOCL;     /* goto decision switch = FGOTYP                */
extern DESCR_t INCLSZ;    /* size of include save block = 4*DESCR         */
extern DESCR_t IOBLSZ;    /* size of I/O blocks = 2*DESCR                 */
extern DESCR_t LNODSZ;    /* long pattern node = NODESZ+DESCR             */
extern DESCR_t NODSIZ;    /* short pattern node = NODESZ                  */
extern DESCR_t OCALIM;    /* size of object code block = OCASIZ*DESCR     */
extern DESCR_t ONECL;     /* constant 1                                   */
extern DESCR_t OUTBLK;    /* pointer to OUTPUT block                      */
extern DESCR_t ERRBLK;    /* pointer to PUNCH (stderr) block [PLB8]       */
extern DESCR_t SIZLMT;    /* limit on size of data object = SIZLIM        */
extern DESCR_t SNODSZS;   /* small pattern node = NODESZ                  */
extern DESCR_t SNODSZ;    /* small pattern node size (NODESZ, type P)     */
extern DESCR_t ARBACK;    /* ARBAK pattern (ARB backup node)               */
/* Pattern function descriptor constants */
extern DESCR_t ANYCCL;    /* ANY function descriptor  (FNC)               */
extern DESCR_t NNYCCL;    /* NOTANY function descriptor (FNC)             */
extern DESCR_t SPNCCL;    /* SPAN function descriptor (FNC)               */
extern DESCR_t LNTHCL;    /* LEN function descriptor  (FNC)               */
extern DESCR_t POSICL;    /* POS function descriptor  (FNC)               */
extern DESCR_t RPSICL;    /* RPOS function descriptor (FNC)               */
extern DESCR_t RTBCL;     /* RTAB function descriptor (FNC)               */
extern DESCR_t TBCL;      /* TAB function descriptor  (FNC)               */
/* Size scratch globals */
extern DESCR_t XSIZ;      /* size scratch X                               */
extern DESCR_t YSIZ;      /* size scratch Y                               */
extern DESCR_t TSIZ;      /* size scratch T                               */
extern DESCR_t ZSIZ;      /* size scratch Z                               */
extern DESCR_t TVAL;      /* value scratch T                              */
extern DESCR_t STARSZ;    /* EXPRESSION pattern size = 11*DESCR           */
extern DESCR_t ZEROCL;    /* constant zero                                */
extern DESCR_t TRSKELS;   /* trace skeleton pointer                       */
extern DESCR_t COMDCT;    /* compiler descriptor count = 15*DESCR [PLB58] */
extern DESCR_t COMREG;    /* pointer to compiler descriptors              */
extern DESCR_t OBEND;     /* end of bin list                              */

/* ── Counters and accumulators ───────────────────────────────────────── */

extern DESCR_t ARTHCL;    /* number of arithmetic operations              */
extern DESCR_t RSTAT;     /* number of reads                              */
extern DESCR_t SCNCL;     /* number of scanner entrances                  */
extern DESCR_t WSTAT;     /* number of writes                             */
extern DESCR_t TIMECL;    /* millisecond time at compiler start           */
extern DESCR_t VARSYM;    /* count of variables                           */
extern DESCR_t GCNO;      /* number of GC cycles                          */
extern DESCR_t ESAICL;    /* current syntactic error count                */

/* ── GC working storage ──────────────────────────────────────────────── */

extern DESCR_t GCMPTR;    /* GC mark working pointer                      */
extern DESCR_t GCREQ;     /* GC requested space                           */
extern DESCR_t GCGOT;     /* GC space obtained                            */
extern DESCR_t GCBLK;     /* GC pseudo-block                              */
extern DESCR_t GCTMCL;    /* GC timer start                               */
extern DESCR_t TEMPCL;    /* temp for timing                              */
extern DESCR_t BKDX;      /* block size (GC) = X_BKSIZE result            */
extern DESCR_t BKDXU;     /* block size unsigned (GC)                     */
extern DESCR_t BKPTR;     /* bin pointer (GC)                             */
extern DESCR_t ST1PTR;    /* string pointer 1 (GC)                        */
extern DESCR_t ST2PTR;    /* string pointer 2 (GC)                        */
extern DESCR_t CPYCL;     /* copy pointer (GC compaction)                 */
extern DESCR_t TTLCL;     /* title pointer (GC walk)                      */
extern DESCR_t TOPCL;     /* top pointer (GC)                             */
extern DESCR_t OFSET;     /* offset (GC relocation)                       */
extern DESCR_t DESCL;     /* descriptor (GC relocation)                   */
extern DESCR_t BLOCL;     /* block pointer (BLOCK/GC)                     */
extern DESCR_t ARG1CL;    /* argument to BLOCK                            */
extern DESCR_t BK1CL;     /* GCM block pointer                            */
extern DESCR_t NODPCL;    /* no-delete switch (GC)                        */
extern DESCR_t PRMDX;     /* permanent block index                        */

/* ── BLOCK/GENVAR working storage ────────────────────────────────────── */

extern DESCR_t BUKPTR;    /* bucket pointer (symbol table)                */
extern DESCR_t LSTPTR;    /* list pointer (symbol table)                  */
extern DESCR_t LCPTR;     /* local pointer (GENVAR)                       */

/* ── Compiler working storage ────────────────────────────────────────── */

extern DESCR_t STNOCL;    /* current statement number                     */
extern DESCR_t LNNOCL;    /* current line number                          */
extern DESCR_t FILENM;    /* current filename (&FILE)                     */
extern DESCR_t CSTNCL;    /* compiler statement number [PLB123]           */
extern DESCR_t STYPE;     /* scanner token type                           */
extern DESCR_t BRTYPE;    /* break type from STREAM                       */
extern DESCR_t FBLOCK;    /* function block pointer                       */
extern DESCR_t NEXFCL;    /* next function position                       */
extern DESCR_t NXFCLS;    /* next function slot                           */
extern DESCR_t FNLIST;    /* function pair list pointer                   */
extern DESCR_t ICLBLK;    /* miscellaneous data block                     */
extern DESCR_t INITB;     /* initialization data pointer                  */
extern DESCR_t INITE;     /* initialization data end                      */
extern DESCR_t INITLS;    /* initialization list                          */
extern DESCR_t PRMPTR;    /* permanent block pointer table                */

/* ── KNLIST — writable keyword pair list ─────────────────────────────── */
/* Each pair: value DESCR (type I, A field = current value)              */
/*            name  DESCR (A field = arena offset of STRING)            */

extern DESCR_t TRIMCL;    /* &TRIM         (init=0)                       */
extern DESCR_t TRAPCL;    /* &TRACE        (init=0)                       */
extern DESCR_t EXLMCL;    /* &STLIMIT      (init=-1) [PLB33]              */
extern DESCR_t MLENCL;    /* &MAXLNGTH     (init=SIZLIM) [PLB81]          */
extern DESCR_t GCTRCL;    /* &GTRACE       (init=0) [PLB92]               */
extern DESCR_t FTLLCL;    /* &FATALLIMIT   (init=0) [PLB128]              */
extern DESCR_t ERRLCL;    /* &ERRLIMIT     (init=0)                       */
extern DESCR_t DMPCL;     /* &DUMP         (init=0)                       */
extern DESCR_t RETCOD;    /* &CODE         (init=0)                       */
extern DESCR_t CASECL;    /* &CASE         (init=1) [PLB15]               */
extern DESCR_t ANCCL;     /* &ANCHOR       (init=0)                       */
extern DESCR_t ABNDCL;    /* &ABEND        (init=0)                       */
extern DESCR_t TRACL;     /* &FTRACE       (init=0)                       */
extern DESCR_t UINTCL;    /* user interrupt flag [PLB109]                 */

/* ── KVLIST — read-only/protected keyword pair list ──────────────────── */

extern DESCR_t ERRTYP;    /* &ERRTYPE      (init=0)                       */
extern DESCR_t ERRTXT;    /* &ERRTEXT      (init="") [PLB38]              */
extern DESCR_t ARBPAT;    /* &ARB          pattern descriptor             */
extern DESCR_t BALPAT;    /* &BAL          pattern descriptor             */
extern DESCR_t FNCPAT;    /* &FENCE        pattern descriptor             */
extern DESCR_t ABOPAT;    /* &ABORT        pattern descriptor             */
extern DESCR_t FALPAT;    /* &FAIL         pattern descriptor             */
extern DESCR_t REMPAT;    /* &REM          pattern descriptor             */
extern DESCR_t SUCPAT;    /* &SUCCEED      pattern descriptor             */
extern DESCR_t LNNOCL_kv; /* &LINE         [PLB38] (KVLIST copy)          */
extern DESCR_t LSFLNM;    /* &LASTFILE     [PLB38]                        */
extern DESCR_t LSLNCL;    /* &LASTLINE     [PLB38]                        */
extern DESCR_t FALCL;     /* &STFCOUNT                                    */
extern DESCR_t LSTNCL;    /* &LASTNO                                      */
extern DESCR_t RETPCL;    /* &RTNTYPE                                     */
extern DESCR_t STNOCL_kv; /* &STNO         [PLB113] (KVLIST copy)         */
extern DESCR_t ALPHVL;    /* &ALPHABET                                    */
extern DESCR_t EXNOCL;    /* &STCOUNT                                     */
extern DESCR_t LVLCL;     /* &FNCLEVEL                                    */
extern DESCR_t LCASVL;    /* &LCASE        [PLB18]                        */
extern DESCR_t UCASVL;    /* &UCASE        [PLB18]                        */
extern DESCR_t PARMVL;    /* &PARM         [PLB39]                        */
extern DESCR_t DIGSVL;    /* &DIGITS       [PLB105]                       */
extern DESCR_t EXN2CL;    /* &STEXEC       [PLB107]                       */
extern DESCR_t GCTTLL;    /* &GCTIME       [PLB107]                       */
extern DESCR_t FATLCL;    /* &FATAL        [PLB128]                       */

/* ── Real-valued keywords ────────────────────────────────────────────── */

extern real_t  PI_val;    /* &PI = 3.14159... [PLB106] — real_t PI_val    */
extern real_t  GCTTLL_val;/* &GCTIME accumulator (real)                   */
extern real_t  RZERCL;    /* real constant 0.0 [PLB104]                   */
extern real_t  R1MCL;     /* real constant 1e6 [PLB107]                   */

/* ── I/O units ───────────────────────────────────────────────────────── */

extern DESCR_t INPUT;     /* INPUT  unit descriptor (UNITI)               */
extern DESCR_t OUTPUT;    /* OUTPUT unit descriptor (UNITO)               */
extern DESCR_t PUNCH;     /* PUNCH  unit descriptor (UNITP)               */
extern DESCR_t TERMIN;    /* TERMINAL input unit (UNITT) [PLB36]          */
extern DESCR_t UNIT;      /* current active input unit                    */
extern DESCR_t DFLSIZ;    /* default input size = VLRECL [PLB129]         */
extern DESCR_t IOKEY;     /* I/O error key                                */

/* ── Pair lists ──────────────────────────────────────────────────────── */

extern DESCR_t DTLIST;    /* data type pair list                          */
extern DESCR_t KNLIST;    /* writable keyword pair list (KNLIST header)   */
extern DESCR_t KVLIST;    /* protected keyword pair list (KVLIST header)  */
extern DESCR_t INLIST;    /* input association pair list [PLB36]          */
extern DESCR_t OTLIST;    /* output association pair list                 */
extern DESCR_t OTSATL;    /* output block list                            */
extern DESCR_t INSATL;    /* input block list                             */
extern DESCR_t TRLIST;    /* trace type pair list                         */
extern DESCR_t TVALPL;    /* value trace pair list                        */
extern DESCR_t TLABPL;    /* label trace pair list                        */
extern DESCR_t TFENPL;    /* call trace pair list                         */
extern DESCR_t TFEXPL;    /* return trace pair list                       */
extern DESCR_t TKEYPL;    /* keyword trace pair list                      */
extern DESCR_t INATL;     /* input attribute list                         */
extern DESCR_t OUTATL;    /* output attribute list                        */
extern DESCR_t TVALL;     /* value trace list head                        */
extern DESCR_t TLABL;     /* label trace list head                        */
extern DESCR_t TFENTL;    /* call trace list head                         */
extern DESCR_t TFEXTL;    /* return trace list head                       */
extern DESCR_t TKEYL;     /* keyword trace list head                      */
extern DESCR_t ATPTR;     /* attribute trace pointer                      */

/* ── Data type pairs (for ARITH dispatch) ────────────────────────────── */

extern DESCR_t ATDTP;     /* ARRAY-TABLE                                  */
extern DESCR_t IIDTP;     /* INTEGER-INTEGER                              */
extern DESCR_t IPDTP;     /* INTEGER-PATTERN                              */
extern DESCR_t IRDTP;     /* INTEGER-REAL                                 */
extern DESCR_t IVDTP;     /* INTEGER-STRING                               */
extern DESCR_t PIDTP;     /* PATTERN-INTEGER                              */
extern DESCR_t PPDTP;     /* PATTERN-PATTERN                              */
extern DESCR_t PVDTP;     /* PATTERN-STRING                               */
extern DESCR_t RIDTP;     /* REAL-INTEGER                                 */
extern DESCR_t RPDTP;     /* REAL-PATTERN                                 */
extern DESCR_t RRDTP;     /* REAL-REAL                                    */
extern DESCR_t RVDTP;     /* REAL-STRING                                  */
extern DESCR_t TADTP;     /* TABLE-ARRAY                                  */
extern DESCR_t VCDTP;     /* STRING-CODE                                  */
extern DESCR_t VEDTP;     /* STRING-EXPRESSION                            */
extern DESCR_t VIDTP;     /* STRING-INTEGER                               */
extern DESCR_t VPDTP;     /* STRING-PATTERN                               */
extern DESCR_t VRDTP;     /* STRING-REAL                                  */
extern DESCR_t VVDTP;     /* STRING-STRING                                */

/* ── Null value ──────────────────────────────────────────────────────── */

extern DESCR_t NULVCL;    /* null/empty value descriptor — v=S, a=0      */

/* ── Function descriptors (FNC bit set) ──────────────────────────────── */
/* Named after their SIL names, used by INVOKE dispatch                  */

extern DESCR_t ANYCL;  extern DESCR_t ASGNCL;  extern DESCR_t ATOPCL;
extern DESCR_t BASECL; extern DESCR_t BRKCCL;  extern DESCR_t BRXCCL;
extern DESCR_t BRXFCL; extern DESCR_t CHRCL;   extern DESCR_t CMACL;
extern DESCR_t CONCL;  extern DESCR_t DNMECL;  extern DESCR_t DNMICL;
extern DESCR_t ENDCL;  extern DESCR_t ENMECL;  extern DESCR_t ENMICL;
extern DESCR_t ERORCL; extern DESCR_t FNCFCL;  extern DESCR_t LITFN;
extern DESCR_t LIT1CL; extern DESCR_t NMECL;   extern DESCR_t NNYCL;
extern DESCR_t SCONCL; extern DESCR_t SCOKCL;  extern DESCR_t SALICL;
extern DESCR_t STARCCL;extern DESCR_t DSARCL;  extern DESCR_t FNCECL;
extern DESCR_t SUCCCL; extern DESCR_t ABORCL;  extern DESCR_t CONTCL;
extern DESCR_t SCNTCL; extern DESCR_t FRETCL;  extern DESCR_t ENDPTR;
extern DESCR_t EXTPTR; extern DESCR_t NRETCL;  extern DESCR_t RETCL;
extern DESCR_t EFFCL;

/* ── TRCBLK — trace function skeleton ───────────────────────────────── */

extern DESCR_t TRCBLK[6];   /* trace block: [0]=fn descr, [1]=LIT1 fn,   */
                              /* [2]=var slot, [3]=LIT1 fn, [4]=tag slot  */

/* ── Primitive patterns (arena offsets stored in named DESCRs) ───────── */

extern DESCR_t ARBBACK;   /* ARBAK pattern (backup ARB)                   */
extern DESCR_t ARHEAD;    /* ARHED pattern (ARB head)                     */
extern DESCR_t ARTAIL;    /* ARTAL pattern (ARB tail)                     */
extern DESCR_t STRPAT;    /* STARPT pattern (unevaluated expression)      */

/* ── Symbol table bin array ──────────────────────────────────────────── */
/* OBARY = OBSIZ+3 bins; each holds a chain head arena offset            */

extern DESCR_t OBLIST_arr[OBARY];  /* OBLIST = OBSTRT - LNKFLD            */
                                    /* access: OBLIST_arr[hash]             */

/* ── Permanent block pointer table (for GC root marking) ─────────────── */

extern DESCR_t PRMTBL[22];  /* PRMTBL array — all live block roots       */

/* ── Static stacks (arena-allocated in sil_data_init) ───────────────── */
/* Pointers set by sil_data_init(); actual storage is in arena           */

extern DESCR_t *pdl_stack;    /* pattern history list — SPDLSZ DESCRs    */
extern DESCR_t *sil_stack;    /* interpreter stack    — STSIZE  DESCRs   */
extern DESCR_t *obj_code;     /* initial object code  — OCASIZ  DESCRs   */

/* ── Error message strings — verbatim from v311.sil ─────────────────── */

extern const char MSG1[];   /* "Illegal data type"                        */
extern const char MSG2[];   /* "Error in arithmetic operation"            */
extern const char MSG3[];   /* "Erroneous array or table reference"       */
extern const char MSG4[];   /* "Null string in illegal context"           */
extern const char MSG5[];   /* "Undefined function or operation"          */
extern const char MSG6[];   /* "Erroneous prototype"                      */
extern const char MSG7[];   /* "Unknown keyword"                          */
extern const char MSG8[];   /* "Variable not present where required"      */
extern const char MSG9[];   /* "Entry point of function not label"        */
extern const char MSG10[];  /* "Illegal argument to primitive function"   */
extern const char MSG11[];  /* "Reading error"                            */
extern const char MSG12[];  /* "Illegal i/o unit"                         */
extern const char MSG13[];  /* "Limit on defined data types exceeded"     */
extern const char MSG14[];  /* "Negative number in illegal context"       */
extern const char MSG15[];  /* "String overflow"                          */
extern const char MSG16[];  /* "Overflow during pattern matching"         */
extern const char MSG17[];  /* "Error in SNOBOL4 system"                  */
extern const char MSG18[];  /* "Return from level zero"                   */
extern const char MSG19[];  /* "Failure during goto evaluation"           */
extern const char MSG20[];  /* "Insufficient storage to continue"         */
extern const char MSG21[];  /* "Stack overflow"                           */
extern const char MSG22[];  /* "Limit on statement execution exceeded"    */
extern const char MSG23[];  /* "Object exceeds size limit"                */
extern const char MSG24[];  /* "Undefined or erroneous goto"              */
extern const char MSG25[];  /* "Incorrect number of arguments"            */
extern const char MSG26[];  /* "Limit on compilation errors exceeded"     */
extern const char MSG27[];  /* "Erroneous END statement"                  */
extern const char MSG28[];  /* "Execution of statement with compilation error" */
extern const char MSG29[];  /* "Erroneous INCLUDE statement"              */
extern const char MSG30[];  /* "Cannot open INCLUDE file"                 */
extern const char MSG31[];  /* "Erroneous LINE statement"                 */
extern const char MSG32[];  /* "Missing END statement"                    */
extern const char MSG33[];  /* "Output error"                             */
extern const char MSG34[];  /* "User interrupt"                           */
extern const char MSG35[];  /* "Not in a SETEXIT handler"                 */
extern const char MSG39[];  /* "Cannot CONTINUE from FATAL error"         */

/* MSGNO: pointer array indexed by error code (1-based) */
extern const char *MSGNO[];

/* ── Compiler error messages — verbatim from v311.sil ───────────────── */

extern const char EMSG1[];   /* "Erroneous label"                         */
extern const char EMSG2[];   /* "Previously defined label"                */
extern const char EMSG3[];   /* "Erroneous subject"                       */
extern const char EMSG14[];  /* "Error in goto"                           */
extern const char ILCHAR[];  /* "Illegal character in element"            */
extern const char ILLBIN[];  /* "Binary operator missing or in error"     */
extern const char ILLBRK[];  /* "Erroneous or missing break character"    */
extern const char ILLDEC[];  /* "Erroneous real number"                   */
extern const char ILLEOS[];  /* "Improperly terminated statement"         */
extern const char ILLINT[];  /* "Erroneous integer"                       */
extern const char OPNLIT[];  /* "Unclosed literal"                        */

/* ── Format strings — verbatim from v311.sil ─────────────────────────── */

extern const char ALOCFL[];  /* "Insufficient storage for initialization\n" */
extern const char ARTHN0[];  /* "%d Arithmetic operations performed\n"    */
extern const char CMTIME[];  /* "%.3f ms. Compilation time\n"             */
extern const char EJECTF[];  /* "\f"                                      */
extern const char ERRCF[];   /* "ERRORS DETECTED IN SOURCE PROGRAM\n\n"  */
extern const char EXNO[];    /* "%d Statements executed, %d failed\n"     */
extern const char FTLCF[];   /* "%s:%d: Error %d in statement %d at level %d\n" */
extern const char GCFMT[];   /* "%s:%d: GC %.3f ms %d free\n"            */
extern const char INCGCF[];  /* "INCOMPLETE STORAGE REGENERATION.\n"      */
extern const char INTIME[];  /* "%.3f ms. Execution time\n"               */
extern const char KSTSTF[];  /* "%.3f Thousand statements per second\n"   */
extern const char LASTSF[];  /* "%s:%d: Last statement executed was %d\n" */
extern const char NODMPF[];  /* "TERMINAL DUMP NOT POSSIBLE.\n"           */
extern const char NRMEND[];  /* "Normal termination at level %d\n"        */
extern const char NVARF[];   /* "Natural variables\n\n"                   */
extern const char PKEYF[];   /* "\nUnprotected keywords\n\n"              */
extern const char PRTOVF[];  /* "***PRINT REQUEST TOO LONG***\n"          */
extern const char READN0[];  /* "%d Reads performed\n"                    */
extern const char SCANNO[];  /* "%d Pattern matches performed\n"          */
extern const char SOURCF[];  /* "    Bell Telephone Laboratories...\n\n"  */
extern const char STATHD[];  /* "SNOBOL4 statistics summary-\n"           */
extern const char STDMP[];   /* "\fDump of variables at termination\n\n"  */
extern const char STGENO[];  /* "%d Regenerations of dynamic storage\n"   */
extern const char STGETM[];  /* "%.3f ms. Execution time in GC\n"        */
extern const char SUCCF[];   /* "No errors detected in source program\n\n" */
extern const char SYSCMT[];  /* "%s:%d: Caught signal %d in statement %d at level %d\n" */
extern const char TIMEPS[];  /* "%.3f ns. Average per statement executed\n" */
extern const char TITLEF[];  /* "SNOBOL4 (Version 3.11, May 19, 1975)\n"  */
extern const char WRITNO[];  /* "%d Writes performed\n"                   */

/* ── String constants (keyword/function names) ───────────────────────── */
/* Named exactly as in v311.sil §23; used by symbol table init           */

extern const char CLERSP[], CODESP[], CLSP[],   CNOSP[],  CNVTSP[];
extern const char CONTSP[], COPYSP[], DATSP[],  DATASP[], DEFISP[];
extern const char DIFFSP[], DGNMSP[], DTCHSP[], DTSP[],   DUMPSP[];
extern const char DUPLSP[], ENDSP[],  ENDFSP[], EQSP[],   ERRLSP[];
extern const char ERRTSP[], ERTXSP[], EVALSP[], EXPSP[],  FAILSP[];
extern const char FATLSP[], FTLLSP[], FNCESP[], FLDSSP[], FNCLSP[];
extern const char FREZSP[], FRETSP[], FTRQSP[], FULLSP[], FUNTSP[];
extern const char GCTMSP[], GESP[],   GTSP[],   GTRQSP[], IDENSP[];
extern const char INSP[],   INTGSP[], ITEMSP[], TRKRSP[], LABLSP[];
extern const char LABCSP[], LSTNSP[], LCNMSP[], LENSP[],  LESP[];
extern const char LEQSP[],  LGESP[],  LGTSP[],  LLESP[],  LLTSP[];
extern const char LNESP[],  LOADSP[], LOCSP[],  LPADSP[], LTSP[];
extern const char MAXLSP[], MAXISP[], NAMESP[], NESP[],   NNYSP[];
extern const char NRETSP[], NUMSP[],  OPSNSP[], OUTSP[],  PARMSP[];
extern const char PATSP[],  PISP[],   POSSP[],  PRTSP[],  RLSP[];
extern const char REMSP_s[],REMDSP[], RETSP[],  REVRSP[], REWNSP[];
extern const char RPLCSP[], RPOSSP[], RPADSP[], RSRTSP[], RTABSP[];
extern const char RTYPSP[], SETSP[],  SETXSP[], SCNTSP[], SIZESP[];
extern const char SSTRSP[], SORTSP[], SPANSP[], STCTSP[], STFCSP[];
extern const char STLMSP[], SPTPSP[], STXTSP[], STNOSP[], VARSP[];
extern const char SUCCSP[], TABSP[],  TERMSP[], THAWSP[], TIMSP[];
extern const char TRCESP[], TRMSP[],  UCNMSP[], UNLDSP[], VALSP[];
extern const char VDIFSP[], ANCHSP[], ABNDSP[], FILESP[], LINESP[];
extern const char LSFNSP[], LSLNSP[], STFCSP_kv[], LSTNSP_kv[];
extern const char ALNMSP[], STCTSP_kv[], FNCLSP_kv[], DIGSP[];
extern const char ALPHSP[], EXDTSP[];
extern const char CRDFSP[]; /* "(80A1)"      default output format        */
extern const char OUTPSP[]; /* "(1X,132A1)"  standard print format        */
extern const char ABORCL_s[],CONTSP_s[], SCNTSP_s[], FSSP[], KSP[];
extern const char LSP[], RSP[], VESP[];

/* ── Initialization function ─────────────────────────────────────────── */

/* sil_data_init: called once from BEGIN to set up all static data       */
/* Allocates pdl_stack, sil_stack, obj_code from arena;                  */
/* sets initial DESCR_t values matching v311.sil declarations            */
void sil_data_init(void);

#endif /* SIL_DATA_H */
