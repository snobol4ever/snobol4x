/*
 * data.c — definitions of all named globals from v311.sil §23 Data
 *
 * Faithful C translation of v311.sil §23 "Data" (lines 10481–12293).
 * Each SIL "X DESCR a,f,v" becomes: DESCR_t X = {.a={.i=a}, .f=f, .v=v};
 * Initial values match the SIL declarations exactly.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M1
 */

#include <stddef.h>
#include <string.h>
#include "types.h"
#include "data.h"

/* ── Arena base (defined here, referenced everywhere) ────────────────── */

char *arena_base = NULL;   /* set by arena_init() */

/* ── Helper macro for initializing DESCR_t ──────────────────────────── */
/* D(a,f,v) matches SIL "DESCR a, f, v"                                 */

#define D(a_val, f_val, v_val) \
    { .a = { .i = (int_t)(a_val) }, .f = (uint8_t)(f_val), .v = (int_t)(v_val) }

/* Zero descriptor — SIL "DESCR 0,0,0" */
#define D0   D(0, 0, 0)

/* ── Scratch DESCRs ──────────────────────────────────────────────────── */

DESCR_t XPTR = D0, YPTR = D0, ZPTR = D0, WPTR = D0, TPTR = D0;
DESCR_t XCL  = D0, YCL  = D0, ZCL  = D0, WCL  = D0, TCL  = D0;
DESCR_t TMVAL = D0, SCL = D0, AXPTR = D0;
DESCR_t XSPPTR = D0, YSPPTR = D0;

/* ── Scratch SPECs ───────────────────────────────────────────────────── */

SPEC_t XSP = {0}, YSP = {0}, ZSP = {0}, WSP = {0}, TSP = {0};
SPEC_t TXSP = {0}, TEXTSP = {0}, NEXTSP = {0}, HEADSP = {0}, LNBFSP = {0};
SPEC_t SPECR1 = {0}, SPECR2 = {0};
SPEC_t DPSP = {0};

/* ── Interpreter registers ───────────────────────────────────────────── */

DESCR_t OCBSCL = D0, OCICL = D0;
DESCR_t CMBSCL = D0, CMOFCL = D0;
DESCR_t PATBCL = D0, PATICL = D0;
DESCR_t FRTNCL = D0, OCSVCL = D0;

/* ── Pattern/Naming stacks ───────────────────────────────────────────── */

DESCR_t PDLPTR = D0, PDLHED = D0, PDLEND = D0;
DESCR_t NAMICL = D0, NHEDCL = D0;
DESCR_t SCBSCL = D0, NBSPTR = D0;

/* ── System stack ────────────────────────────────────────────────────── */

DESCR_t STKHED = D0, STKEND = D0, STKPTR = D0;

/* ── Arena / storage management ──────────────────────────────────────── */

DESCR_t FRSGPT = D0, TLSGP1 = D0, HDSGPT = D0, MVSGPT = D0, BKLTCL = D0;

/* ── Switches — initial values match v311.sil declarations ───────────── */

DESCR_t ALCL   = D0;
DESCR_t ARRMRK = D0;
DESCR_t BANRCL = D(1, 0, 0);   /* display startup banner = 1            */
DESCR_t COMPCL = D(1, 0, 0);   /* compiling = 1                         */
DESCR_t CUTNO  = D0;
DESCR_t CNSLCL = D0;
DESCR_t DATACL = D0;
DESCR_t EXECCL = D(1, 0, 0);   /* EXECUTE switch = 1 [PLB34]            */
DESCR_t FNVLCL = D0;
DESCR_t HIDECL = D0;
DESCR_t INICOM = D0;
DESCR_t LENFCL = D0;
DESCR_t LISTCL = D0;
DESCR_t LLIST  = D(1, 0, 0);   /* left listing = 1 [PLB12]              */
DESCR_t NAMGCL = D0;
DESCR_t NERRCL = D(1, 0, 0);   /* NOERROR = 1 [PLB9]                    */
DESCR_t SCERCL = D0;
DESCR_t SPITCL = D(1, 0, I);   /* SPITBOL features = 1 [PLB32]          */
DESCR_t STATCL = D0;
DESCR_t BLOKCL = D(0, 0, I);   /* SIL: BLOKCL DESCR 0,0,I — BLOCKS feature flag (skipped but data present) */
DESCR_t INSW   = D(1, 0, I);   /* &INPUT = 1                            */
DESCR_t OUTSW  = D(1, 0, I);   /* &OUTPUT = 1                           */
DESCR_t FULLCL = D0;
DESCR_t CONVSW = D0;

/* ── KNLIST — writable keywords ──────────────────────────────────────── */

DESCR_t TRIMCL = D(0, 0, I);           /* &TRIM = 0                     */
DESCR_t TRAPCL = D(0, 0, I);           /* &TRACE = 0                    */
DESCR_t EXLMCL = D(-1, 0, I);          /* &STLIMIT = -1 [PLB33]         */
DESCR_t MLENCL = D(0x7fffffff, 0, I);  /* &MAXLNGTH = SIZLIM [PLB81]    */
DESCR_t GCTRCL = D(0, 0, I);           /* &GTRACE = 0 [PLB92]           */
DESCR_t FTLLCL = D(0, 0, I);           /* &FATALLIMIT = 0 [PLB128]      */
DESCR_t ERRLCL = D(0, 0, I);           /* &ERRLIMIT = 0                 */
DESCR_t DMPCL  = D(0, 0, I);           /* &DUMP = 0                     */
DESCR_t RETCOD = D(0, 0, I);           /* &CODE = 0                     */
DESCR_t CASECL = D(1, 0, I);           /* &CASE = 1 (folding) [PLB15]   */
DESCR_t ANCCL  = D(0, 0, I);           /* &ANCHOR = 0                   */
DESCR_t ABNDCL = D(0, 0, I);           /* &ABEND = 0                    */
DESCR_t TRACL  = D(0, 0, I);           /* &FTRACE = 0                   */
DESCR_t UINTCL = D0;

/* ── KVLIST — read-only keyword DESCRs ───────────────────────────────── */

DESCR_t ERRTYP    = D(0, 0, I);  /* &ERRTYPE                            */
DESCR_t ERRTXT    = D(0, 0, S);  /* &ERRTEXT [PLB38]                    */
DESCR_t ARBPAT    = D(0, 0, P);   /* &ARB    — .a filled in data_init */
DESCR_t BALPAT    = D(0, 0, P);   /* &BAL    — .a filled in data_init */
DESCR_t FNCPAT    = D(0, 0, P);   /* &FENCE  — .a filled in data_init */
DESCR_t ABOPAT    = D(0, 0, P);   /* &ABORT  — .a filled in data_init */
DESCR_t FALPAT    = D(0, 0, P);   /* &FAIL   — .a filled in data_init */
DESCR_t REMPAT    = D(0, 0, P);   /* &REM    — .a filled in data_init */
DESCR_t SUCPAT    = D(0, 0, P);   /* &SUCCEED — .a filled in data_init */
DESCR_t LNNOCL_kv = D(0, 0, I);  /* &LINE [PLB38]                       */
DESCR_t LSFLNM   = D(0, 0, S);   /* &LASTFILE [PLB38]                   */
DESCR_t LSLNCL   = D(0, 0, I);   /* &LASTLINE [PLB38]                   */
DESCR_t FALCL    = D(0, 0, I);   /* &STFCOUNT                           */
DESCR_t LSTNCL   = D(0, 0, I);   /* &LASTNO                             */
DESCR_t RETPCL   = D(0, 0, S);   /* &RTNTYPE                            */
DESCR_t STNOCL_kv = D(0, 0, I);  /* &STNO [PLB113]                      */
DESCR_t ALPHVL   = D0;            /* &ALPHABET                           */
DESCR_t EXNOCL   = D(0, 0, I);   /* &STCOUNT                            */
DESCR_t LVLCL    = D(0, 0, I);   /* &FNCLEVEL                           */
DESCR_t LCASVL   = D0;            /* &LCASE [PLB18]                      */
DESCR_t UCASVL   = D0;            /* &UCASE [PLB18]                      */
DESCR_t PARMVL   = D(0, 0, S);   /* &PARM [PLB39]                       */
/* SIL: DIGSVL DESCR 0,0,0 — &DIGITS value cell [PLB105] */
DESCR_t DIGSVL   = D0;            /* &DIGITS [PLB105] — D0 = 0,0,0 correct */
DESCR_t EXN2CL   = D(0, 0, I);   /* &STEXEC [PLB107]                    */
/* SIL: GCTTTL REAL 0.0 — real-typed DESCR slot [PLB92][PLB107] */
DESCR_t GCTTTL   = {.a={.f=0.0f}, .f=0, .v=R};
/* SIL: gctttl+2  DESCR SMAXINT,0,I — &MAXINT initial value [PLB108][PLB116] */
DESCR_t MAXICL   = {.a={.i=0x7FFFFFFF}, .f=0, .v=I};
DESCR_t FATLCL   = D(0, 0, I);   /* &FATAL [PLB128]                     */

/* ── Real-valued keywords ────────────────────────────────────────────── */

/* SIL: digsvl+2  REAL 3.14159... — &PI value cell [PLB106] */
DESCR_t PIVCL    = {.a={.f=3.14159265358979323846f}, .f=0, .v=R};
DESCR_t RZERCL   = {.a={.f=0.0f},  .f=0, .v=R}; /* real zero [PLB104]  */
DESCR_t R1MCL    = {.a={.f=1.0e6f},.f=0, .v=R}; /* 1e6 [PLB107]        */

/* ── I/O units ───────────────────────────────────────────────────────── */

/* SIL OUTPUT: DESCR UNITO,0,I / DESCR OUTPSP,0,0 — 2-slot block */
DESCR_t OUTPUT[2] = { D(UNITO, 0, I), D0 };  /* [1].a=OUTPSP set in data_init */
/* SIL PUNCH: DESCR UNITP,0,I — 1-slot */
DESCR_t PUNCH  = D(UNITP, 0, I);
/* SIL PCHFST: DESCR CRDFSP,0,0 — 1-slot, .a set in data_init */
DESCR_t PCHFST = D0;
/* SIL INPUT: DESCR UNITI,0,I — 1-slot */
DESCR_t INPUT  = D(UNITI, 0, I);
/* SIL DFLSIZ: DESCR VLRECL,0,I — 1-slot; VLRECL=0 so .a=0 is correct */
DESCR_t DFLSIZ = D(VLRECL, 0, I);
/* SIL TERMIN: DESCR UNITT,0,I / DESCR VLRECL,0,I — 2-slot block */
DESCR_t TERMIN[2] = { D(UNITT, 0, I), D(VLRECL, 0, I) };
DESCR_t UNIT   = D0;            /* current active unit                   */
DESCR_t IOKEY  = D0;

/* ── Counters ────────────────────────────────────────────────────────── */

DESCR_t ARTHCL = D0;
DESCR_t RSTAT  = D0;
DESCR_t SCNCL  = D0;
DESCR_t WSTAT  = D0;
DESCR_t TIMECL = D0;
DESCR_t VARSYM = D0;
DESCR_t GCNO   = D0;
DESCR_t ESAICL = D0;

/* ── GC working storage ──────────────────────────────────────────────── */

DESCR_t GCMPTR = D0, GCREQ = D0, GCGOT = D(0,0,I), GCBLK = D0;
DESCR_t GCTMCL = D0, TEMPCL = D0;
DESCR_t BKDX = D0, BKDXU = D0, BKPTR = D0;
DESCR_t ST1PTR = D0, ST2PTR = D0;
DESCR_t CPYCL = D0, TTLCL = D0, TOPCL = D0, OFSET = D0, DESCL = D0;
DESCR_t BLOCL = D0, ARG1CL = D0, BK1CL = D0, NODPCL = D0, PRMDX = D0;

/* ── BLOCK/GENVAR working storage ────────────────────────────────────── */

DESCR_t BUKPTR = {.a={.i=0},.f=PTR,.v=S}, /* SIL: DESCR 0,PTR,S */
        LSTPTR = {.a={.i=0},.f=PTR,.v=S}, /* SIL: DESCR 0,PTR,S */
        LCPTR  = D0;

/* ── Compiler working storage ────────────────────────────────────────── */

DESCR_t STNOCL = D(0, 0, I), LNNOCL = D(0, 0, I), FILENM = D(0, 0, S), CSTNCL = D(0, 0, I);
DESCR_t STYPE = D0, BRTYPE = D0;
DESCR_t FBLOCK = D0, NEXFCL = D0;
DESCR_t FNLIST = D0;
DESCR_t ICLBLK = {.a={.i=0},.f=TTL|MARK,.v=0};  /* SIL: DESCR ICLBLK,TTL+MARK,size — GC block header */
DESCR_t INITB = D0, INITE = D0, INITLS = D0;
DESCR_t PRMPTR = D0;

/* ── Constants (DESCR-valued) ────────────────────────────────────────── */
/* Values computed in data_init() once DESCR size is known          */

DESCR_t ARBSIZ = D0;    /* 8*NODESZ                                       */
DESCR_t CHARCL = D(1, 0, 0);
DESCR_t CNDSIZ = D0;    /* CNODSZ                                        */
DESCR_t CODELT = D0;    /* 200*DESCR                                      */
DESCR_t DSCRTW = D0;    /* 2*DESCR                                        */
DESCR_t EOSCL  = D(EOSTYP, 0, 0);
DESCR_t ESALIM = D0;    /* ESASIZ*DESCR                                   */
DESCR_t EXTVAL = D0;    /* EXTSIZ*2*DESCR                                 */
DESCR_t FBLKRQ = D0;    /* FBLKSZ                                         */
DESCR_t GOBRCL = D0;
DESCR_t GTOCL  = D(FGOTYP, 0, 0);
DESCR_t INCLSZ = D0;    /* 4*DESCR                                        */
DESCR_t IOBLSZ = D0;    /* 2*DESCR                                        */
DESCR_t LNODSZ = D0;    /* NODESZ+DESCR                                   */
DESCR_t NODSIZ = D0;    /* NODESZ                                         */
DESCR_t OCALIM = D0;    /* OCASIZ*DESCR                                   */
DESCR_t ONECL  = D(1, 0, 0);
DESCR_t TWOCL  = D(2*8, 0, B);  /* v311.sil 11149: 2*DESCR,0,B — block-size constant */
DESCR_t OUTBLK = D0;    /* set in data_init to &OUTPUT[0] - DESCR     */
DESCR_t ERRBLK = D0;    /* set in data_init to &PUNCH  - DESCR        */
/* SIL OTSATL: DESCR OTSATL,TTL+MARK,4*DESCR — output unit table header (self-ref) */
DESCR_t OTSATL = D(0, TTL|MARK, 0); /* .a and .v set in data_init */
/* SIL INSATL: DESCR INSATL,TTL+MARK,2*DESCR — input unit table header (self-ref) */
DESCR_t INSATL = D(0, TTL|MARK, 0); /* .a and .v set in data_init */
/* SIL INLIST (line 10612): DESCR INLIST,TTL+MARK,4*DESCR — input assoc list [PLB36] */
DESCR_t INLIST = D(0, TTL|MARK, 0); /* .a and .v set in data_init */
/* SIL OTLIST (line 10617): DESCR OTLIST,TTL+MARK,4*DESCR — output assoc list */
DESCR_t OTLIST = D(0, TTL|MARK, 0); /* .a and .v set in data_init */
/* SIL TRLIST: DESCR TRLIST,TTL+MARK,20*DESCR / DESCR TVALL,0,0 — trace type list */
DESCR_t TRLIST[2] = { D(0, TTL|MARK, 0), D0 }; /* [0] self-ref header, [0].a+.v and [1].a in data_init */
/* SIL VALTRS: DESCR VALSP,0,0 / DESCR TLABL,0,0 / DESCR TRLASP,0,0 — 3 slots */
DESCR_t VALTRS[3] = { D0, D0, D0 }; /* all .a fields set in data_init */
DESCR_t SIZLMT = D(0x7fffffff, 0, 0);
/* SNODSZ: defined in platform.c */
DESCR_t STARSZ = D0;    /* 11*DESCR                                       */
DESCR_t ZEROCL = D0;
DESCR_t TRSKEL  = D0;    /* trace skeleton pointer (A=TRCBLK — set in data_init) */
DESCR_t COMDCT = D0;    /* 15*DESCR [PLB58]                               */
DESCR_t COMREG = D0;
DESCR_t OBEND  = D0;    /* OBLIST + DESCR*OBOFF — set in arena_init  */
DESCR_t OBPTR  = D0;    /* OBLIST = OBSTRT - LNKFLD — set in arena_init */

/* ── Null value ──────────────────────────────────────────────────────── */

DESCR_t NULVCL = D(0, 0, S);   /* null string — type S, A=0             */

/* ── Data type pairs ─────────────────────────────────────────────────── */

DESCR_t ATDTP = D(A, 0, T);    /* ARRAY-TABLE    */
DESCR_t IIDTP = D(I, 0, I);    /* INTEGER-INTEGER */
DESCR_t IPDTP = D(I, 0, P);    /* INTEGER-PATTERN */
DESCR_t IRDTP = D(I, 0, R);    /* INTEGER-REAL    */
DESCR_t IVDTP = D(I, 0, S);    /* INTEGER-STRING  */
DESCR_t PIDTP = D(P, 0, I);    /* PATTERN-INTEGER */
DESCR_t PPDTP = D(P, 0, P);    /* PATTERN-PATTERN */
DESCR_t PVDTP = D(P, 0, S);    /* PATTERN-STRING  */
DESCR_t RIDTP = D(R, 0, I);    /* REAL-INTEGER    */
DESCR_t RPDTP = D(R, 0, P);    /* REAL-PATTERN    */
DESCR_t RRDTP = D(R, 0, R);    /* REAL-REAL       */
DESCR_t RVDTP = D(R, 0, S);    /* REAL-STRING     */
DESCR_t TADTP = D(T, 0, A);    /* TABLE-ARRAY     */
DESCR_t VCDTP = D(S, 0, C);    /* STRING-CODE     */
DESCR_t VEDTP = D(S, 0, E);    /* STRING-EXPRESSION */
DESCR_t VIDTP = D(S, 0, I);    /* STRING-INTEGER  */
DESCR_t VPDTP = D(S, 0, P);    /* STRING-PATTERN  */
DESCR_t VRDTP = D(S, 0, R);    /* STRING-REAL     */
DESCR_t VVDTP = D(S, 0, S);    /* STRING-STRING   */

/* ── Function descriptors (FNC bit set) ──────────────────────────────── */
/* A field = function pointer (set at runtime by register calls)        */
/* V field = argument count                                              */

/* ANYCCL: defined in platform.c */  DESCR_t ASGNCL = D(0, FNC, 2);
DESCR_t ATOPCL = D(0, FNC, 3);   DESCR_t BASECL = D(0, FNC, 0);
DESCR_t BRKCCL = D(0, FNC, 3);   DESCR_t BRXCCL = D(0, FNC, 3);
DESCR_t BRXFCL = D(0, FNC, 2);   DESCR_t CHRCL  = D(0, FNC, 3);
DESCR_t CMACL  = D(0, FNC, 0);   DESCR_t CONCL  = D(0, FNC, 0);
DESCR_t DNMECL = D(0, FNC, 2);   DESCR_t DNMICL = D(0, FNC, 2);
DESCR_t ENDCL  = D(0, FNC, 0);   DESCR_t ENMECL = D(0, FNC, 3);
DESCR_t ENMICL = D(0, FNC, 3);   DESCR_t ERORCL = D(0, FNC, 1);
DESCR_t FNCFCL = D(0, FNC, 2);   DESCR_t LITFN  = D(0, 0, 1);
/* LIT1CL: 4-slot block per SIL oracle (E3.7.1 / V3.7):
 *   [0] DESCR LITFN,FNC,1   — literal fn descriptor
 *   [1] DESCR 0,0,0         — variable to be traced
 *   [2] DESCR LITFN,FNC,1   — literal fn descriptor (2nd)
 *   [3] DESCR 0,0,0         — tag supplied for trace
 * .a fields [0] and [2] set to P2A(&LITFN) in data_init */
DESCR_t LIT1CL[4] = {
    D(0, FNC, 1),    /* [0] literal fn descriptor — .a filled in data_init */
    D0,              /* [1] variable to be traced */
    D(0, FNC, 1),    /* [2] literal fn descriptor — .a filled in data_init */
    D0               /* [3] tag supplied for trace */
};
DESCR_t NMECL  = D(0, FNC, 2);  /* SIL: DESCR NMEFN,FNC,2 */
/* NNYCCL: defined in platform.c */ DESCR_t SCONCL = D(0, FNC, 2);
DESCR_t SCOKCL = D(0, FNC, 2);   /* SALICL: no oracle counterpart — removed */
/* STARCCL, DSARCL: no oracle counterpart — removed */
/* FNCECL, SUCCCL: no oracle counterpart — removed */
DESCR_t ABORCL = D0;              DESCR_t CONTCL = D0;
DESCR_t SCNTCL = D0;              DESCR_t FRETCL = D0;
DESCR_t ENDPTR = D0;              DESCR_t EXTPTR = D0;
DESCR_t NRETCL = D0;              DESCR_t RETCL  = D0;
DESCR_t EFFCL  = D0;

/* ── TRCBLK — trace function skeleton ────────────────────────────────── */

DESCR_t TRCBLK[6] = {
    D(0, TTL|MARK, 5*DESCR),   /* [0] header                           */
    D(0, FNC, 2),               /* [1] TRACE FUNCTION DESCRIPTOR V3.7  */
    D(0, FNC, 1),               /* [2] LIT1CL — literal fn             */
    D0,                         /* [3] variable to be traced            */
    D(0, FNC, 1),               /* [4] LIT1CL — literal fn             */
    D0                          /* [5] tag for trace                    */
};

/* ── TFNCLP — CALL trace pair-list (2 slots per SIL line 10638) ─── */
/* SIL: TFNCLP DESCR TFENTL,0,0 / DESCR TRFRSP,0,0
 * .a fields filled in data_init (pointer values) */
DESCR_t TFNCLP[2] = { D0, D0 };

/* ── TFNRLP — RETURN/KEYWORD/VALUE/LABEL trace pair-list (14 slots) */
/* SIL lines 10640–10655 [PLB115]: pairs of (trace-list-head, string-literal)
 * for RETURN, KEYWORD, VALUE, LABEL, CALL, RETURN, KEYWORD traces
 * .a fields all filled in data_init */
DESCR_t TFNRLP[14] = {
    D0, D0,   /* [0-1]  TFEXTL / RETSP   — RETURN  */
    D0, D0,   /* [2-3]  TKEYL  / TRKYSP  — KEYWORD */
    D0, D0,   /* [4-5]  TVALL  / VEESP   — VALUE 'V' */
    D0, D0,   /* [6-7]  TLABL  / LSP     — LABEL 'L' */
    D0, D0,   /* [8-9]  TFENTL / CSP     — CALL  'C' */
    D0, D0,   /* [10-11] TFEXTL / RSP    — RETURN 'R' */
    D0, D0    /* [12-13] TKEYL / KSP     — KEYWORD 'K' */
};

/* ── ATRHD / ATPRCL / ATEXCL — TABLE→ARRAY conversion skeleton ──── */
/* SIL lines 10663–10670:
 *   ATRHD  DESCR ATPRCL-DESCR,0,0   — array header (A = ATPRCL-DESCR offset)
 *   ATPRCL DESCR 0,0,0              — prototype slot [0]
 *          DESCR 2,0,0              — dimensionality [1]
 *          DESCR 1,0,2              — 1:2 second dimension [2]
 *   ATEXCL DESCR 1,0,0              — 1:n first dimension
 * .a of ATRHD set in data_init */
DESCR_t ATRHD  = D0;           /* .a = P2A(ATPRCL) - DESCR — set in data_init */
DESCR_t ATPRCL[3] = {
    D0,              /* [0] prototype */
    D(2, 0, 0),      /* [1] dimensionality = 2 */
    D(1, 0, 2)       /* [2] 1:2 second dimension */
};
DESCR_t ATEXCL = D(1, 0, 0);   /* 1:n first dimension */

/* ── Primitive patterns (arena offsets — filled by data_init) ────── */

/* ARBACK: defined in platform.c */
DESCR_t ARHEAD  = D0;    /* ARHED arena offset */
DESCR_t ARTAIL  = D0;    /* ARTAL arena offset */
DESCR_t STRPAT  = D0;    /* STARPT arena offset */

/* ── Symbol table bins: now allocated in arena by arena_init() ──────── */
/* OBPTR.a = arena base of OBLIST (= OBSTRT - LNKFLD); OBSLOT(i) in arena.c */

/* ── Permanent block table ────────────────────────────────────────────── */

DESCR_t PRMTBL[19];         /* header + 18 GC roots: PRMTBL(8)+STKHED(11) [v311.sil 11997+12005] */
DESCR_t FTABLE  = D0;    /* procedure descriptor table header    */
DESCR_t OPTBL   = D0;    /* operator procedure descriptor table   */
DESCR_t DTEND   = D0;    /* end-of-data-table marker: A=EFFCL    */

/* ── Stack pointers (set in data_init after arena allocation) ────── */

DESCR_t *pdl_stack  = NULL;
DESCR_t *stack  = NULL;
DESCR_t *obj_code   = NULL;

/* ── Error messages — verbatim from v311.sil MSG1..MSG39 [PLB11] ─────── */

const char MSG1[]  = "Illegal data type";
const char MSG2[]  = "Error in arithmetic operation";
const char MSG3[]  = "Erroneous array or table reference";
const char MSG4[]  = "Null string in illegal context";
const char MSG5[]  = "Undefined function or operation";
const char MSG6[]  = "Erroneous prototype";
const char MSG7[]  = "Unknown keyword";
const char MSG8[]  = "Variable not present where required";
const char MSG9[]  = "Entry point of function not label";
const char MSG10[] = "Illegal argument to primitive function";
const char MSG11[] = "Reading error";
const char MSG12[] = "Illegal i/o unit";
const char MSG13[] = "Limit on defined data types exceeded";
const char MSG14[] = "Negative number in illegal context";
const char MSG15[] = "String overflow";
const char MSG16[] = "Overflow during pattern matching";
const char MSG17[] = "Error in SNOBOL4 system";
const char MSG18[] = "Return from level zero";
const char MSG19[] = "Failure during goto evaluation";
const char MSG20[] = "Insufficient storage to continue";
const char MSG21[] = "Stack overflow";
const char MSG22[] = "Limit on statement execution exceeded";
const char MSG23[] = "Object exceeds size limit";
const char MSG24[] = "Undefined or erroneous goto";
const char MSG25[] = "Incorrect number of arguments";
const char MSG26[] = "Limit on compilation errors exceeded";
const char MSG27[] = "Erroneous END statement";
const char MSG28[] = "Execution of statement with compilation error";
const char MSG29[] = "Erroneous INCLUDE statement";
const char MSG30[] = "Cannot open INCLUDE file";
const char MSG31[] = "Erroneous LINE statement";
const char MSG32[] = "Missing END statement";
const char MSG33[] = "Output error";
const char MSG34[] = "User interrupt";
const char MSG35[] = "Not in a SETEXIT handler";
const char MSG39[] = "Cannot CONTINUE from FATAL error";

/* MSGNO: 1-based pointer array — MSG[errcode] → message string          */
/* (unused entries = NULL; BLOCKS msgs 36-38 omitted)                    */
const char *MSGNO[] = {
    NULL,     /* [0] unused */
    MSG1,  MSG2,  MSG3,  MSG4,  MSG5,  MSG6,  MSG7,  MSG8,  MSG9,  MSG10,
    MSG11, MSG12, MSG13, MSG14, MSG15, MSG16, MSG17, MSG18, MSG19, MSG20,
    MSG21, MSG22, MSG23, MSG24, MSG25, MSG26, MSG27, MSG28, MSG29, MSG30,
    MSG31, MSG32, MSG33, MSG34, MSG35,
    NULL, NULL, NULL,   /* 36-38: BLOCKS — skipped */
    MSG39
};

/* ── Compiler error messages — verbatim from v311.sil [PLB11] ───────── */

const char EMSG1[]  = "Erroneous label";
const char EMSG2[]  = "Previously defined label";
const char EMSG3[]  = "Erroneous subject";
const char EMSG14[] = "Error in goto";
const char ILCHAR[] = "Illegal character in element";
const char ILLBIN[] = "Binary operator missing or in error";
const char ILLBRK[] = "Erroneous or missing break character";
const char ILLDEC[] = "Erroneous real number";
const char ILLEOS[] = "Improperly terminated statement";
const char ILLINT[] = "Erroneous integer";
const char OPNLIT[] = "Unclosed literal";

/* ── Format strings — verbatim from v311.sil [PLB10][PLB38] ─────────── */

const char ALOCFL[] = "Insufficient storage for initialization\n";
const char ARTHNO[] = "%d Arithmetic operations performed\n";
const char CMTIME[] = "%.3f ms. Compilation time\n";
const char EJECTF[] = "\f";
const char ERRCF[]  = "ERRORS DETECTED IN SOURCE PROGRAM\n\n";
const char EXNO[]   = "%d Statements executed, %d failed\n";
const char FTLCF[]  = "%s:%d: Error %d in statement %d at level %d\n";
const char GCFMT[]  = "%s:%d: GC %.3f ms %d free\n";
const char INCGCF[] = "INCOMPLETE STORAGE REGENERATION.\n";
const char INTIME[] = "%.3f ms. Execution time\n";
const char KSTSF[] = "%.3f Thousand statements per second\n";
const char LASTSF[] = "%s:%d: Last statement executed was %d\n";
const char NODMPF[] = "TERMINAL DUMP NOT POSSIBLE.\n";
const char NRMEND[] = "Normal termination at level %d\n";
const char NVARF[]  = "Natural variables\n\n";
const char PKEYF[]  = "\nUnprotected keywords\n\n";
const char PRTOVF[] = "***PRINT REQUEST TOO LONG***\n";
const char READNO[] = "%d Reads performed\n";
const char SCANNO[] = "%d Pattern matches performed\n";
const char SOURCF[] = "    Bell Telephone Laboratories, Incorporated\n\n";
const char STATHD[] = "SNOBOL4 statistics summary-\n";
const char STDMP[]  = "\fDump of variables at termination\n\n";
const char STGENO[] = "%d Regenerations of dynamic storage\n";
const char STGETM[] = "%.3f ms. Execution time in GC\n";
const char SUCCF[]  = "No errors detected in source program\n\n";
const char SYSCMT[] = "%s:%d: Caught signal %d in statement %d at level %d\n";
const char TIMEPS[] = "%.3f ns. Average per statement executed\n";
const char TITLEF[] = "SNOBOL4 (Version 3.11, May 19, 1975)\n";
const char WRITNO[] = "%d Writes performed\n";

/* ── String constants — keyword and function names ───────────────────── */

const char CLERSP[] = "CLEAR";       const char CODESP[] = "CODE";
const char CLSP[]   = "COLLECT";     const char CNOSP[]  = "COMPNO";
const char CNVTSP[] = "CONVERT";     const char CONTSP[] = "CONTINUE";
const char COPYSP[] = "COPY";        const char DATSP[]  = "DATE";
const char DATASP[] = "DATA";        const char DEFISP[] = "DEFINE";
const char DIFFSP[] = "DIFFER";      const char DGNMSP[] = "DIGITS";
const char DTCHSP[] = "DETACH";      const char DTSP[]   = "DATATYPE";
const char DUMPSP[] = "DUMP";        const char DUPLSP[] = "DUPL";
const char ENDSP[]  = "END";         const char ENDFSP[] = "ENDFILE";
const char EQSP[]   = "EQ";          const char ERRLSP[] = "ERRLIMIT";
const char ERRTSP[] = "ERRTYPE";     const char ERTXSP[] = "ERRTEXT";
const char EVALSP[] = "EVAL";        const char EXPSP[]  = "EXPRESSION";
const char FAILSP[] = "FAIL";        const char FATLSP[] = "FATAL";
const char FTLLSP[] = "FATALLIMIT";  const char FNCESP[] = "FENCE";
const char FLDSSP[] = "FIELD";       const char FNCLSP[] = "FNCLEVEL";
const char FREZSP[] = "FREEZE";      const char FRETSP[] = "FRETURN";
const char FTRCSP[] = "FTRACE";      const char FULLSP[] = "FULLSCAN";
const char FUNTSP[] = "FUNCTION";    const char GCTMSP[] = "GCTIME";
const char GESP[]   = "GE";          const char GTSP[]   = "GT";
const char GTRCSP[] = "GTRACE";      const char IDENSP[] = "IDENT";
const char INSP[]   = "INPUT";       const char INTGSP[] = "INTEGER";
const char ITEMSP[] = "ITEM";        const char TRKRSP[] = "KEYWORD";
const char LABLSP[] = "LABEL";       const char LABCSP[] = "LABELCODE";
const char LSTNSP[] = "LASTNO";      const char LCNMSP[] = "LCASE";
const char LENSP[]  = "LEN";         const char LESP[]   = "LE";
const char LEQSP[]  = "LEQ";         const char LGESP[]  = "LGE";
const char LGTSP[]  = "LGT";         const char LLESP[]  = "LLE";
const char LLTSP[]  = "LLT";         const char LNESP[]  = "LNE";
const char LOADSP[] = "LOAD";        const char LOCSP[]  = "LOCAL";
const char LPADSP[] = "LPAD";        const char LTSP[]   = "LT";
const char MAXLSP[] = "MAXLNGTH";    const char MAXISP[] = "MAXINT";
const char NAMESP[] = "NAME";        const char NESP[]   = "NE";
const char NNYSP[]  = "NOTANY";      const char NRETSP[] = "NRETURN";
const char NUMSP[]  = "NUMERIC";     const char OPSNSP[] = "OPSYN";
const char OUTSP[]  = "OUTPUT";      const char PARMSP[] = "PARM";
const char PATSP[]  = "PATTERN";     const char PISP[]   = "PI";
const char POSSP[]  = "POS";         const char PRTSP[]  = "PROTOTYPE";
const char RLSP[]   = "REAL";        const char REMSP_s[] = "REM";
const char REMDSP[] = "REMDR";       const char RETSP[]  = "RETURN";
const char REVRSP[] = "REVERSE";     const char REWNSP[] = "REWIND";
const char RPLCSP[] = "REPLACE";     const char RPOSSP[] = "RPOS";
const char RPADSP[] = "RPAD";        const char RSRTSP[] = "RSORT";
const char RTABSP[] = "RTAB";        const char RTYPSP[] = "RTNTYPE";
const char SETSP[]  = "SET";         const char SETXSP[] = "SETEXIT";
const char SCNTSP[] = "SCONTINUE";   const char SIZESP[] = "SIZE";
const char SSTRSP[] = "SUBSTR";      const char SORTSP[] = "SORT";
const char SPANSP[] = "SPAN";        const char STCTSP[] = "STCOUNT";
const char STFCSP[] = "STFCOUNT";    const char STLMSP[] = "STLIMIT";
const char SPTPSP[] = "STOPTR";      const char STXTSP[] = "STEXEC";
const char STNOSP[] = "STNO";        const char VARSP[]  = "STRING";
const char SUCCSP[] = "SUCCEED";     const char TABSP[]  = "TAB";
const char TERMSP[] = "TERMINAL";    const char THAWSP[] = "THAW";
const char TIMSP[]  = "TIME";        const char TRCESP[] = "TRACE";
const char TRMSP[]  = "TRIM";        const char UCNMSP[] = "UCASE";
const char UNLDSP[] = "UNLOAD";      const char VALSP[]  = "VALUE";
const char VDIFSP[] = "VDIFFER";     const char ANCHSP[] = "ANCHOR";
const char ABNDSP[] = "ABEND";       const char FILESP[] = "FILE";
/* Missing STRING blocks from v311.sil §24 lines 11783-11920 */
const char ABORSP[]="ABORT";    const char ANYSP[]="ANY";       const char APLYSP[]="APPLY";
const char ARBSP[]="ARB";      const char ARBNSP[]="ARBNO";    const char ARGSP[]="ARG";
const char BACKSP[]="BACKSPACE"; const char BALSP[]="BAL";      const char BRKSP[]="BREAK";
const char BRKXSP[]="BREAKX";  const char CASESP[]="CASE";     const char CHARSP[]="CHAR";
const char REMSP[]="REM";      const char STPTSP[]="STOPTR";   const char BLOKSP[]="BLOCK";
const char BLKSSP[]="BLOCKS";  const char BKGNSP[]="FILL";     const char NOBLSP[]="NOBLOCKS";
const char LINESP[] = "LINE";        const char LSFNSP[] = "LASTFILE";
const char LSLNSP[] = "LASTLINE";    const char ALNMSP[] = "ALPHABET";
/* v311.sil line 10883: ALPHSP SPEC ALPHA,0,0,0,ALPHSZ  (alphabet string specifier)   */
/* v311.sil line 10910: EXDTSP STRING 'EXTERNAL'                                        */
/* Both are SPEC_t; .a = arena offset of string data, .o = 0, .l = length, .v = S.     */
/* Initialised in data_init() once the arena is live.                                   */
SPEC_t ALPHSP, EXDTSP;
const char CRDFSP[] = "(80A1)";
const char OUTPSP[] = "(1X,132A1)";
/* F1SP-F28SP: graphics/format field name strings [v311.sil §24 lines 11921-11948] */
const char F1SP[]="PRINT"; const char F2SP[]="HOR";      const char F3SP[]="VER";
const char F4SP[]="FRONT"; const char F5SP[]="BOX";      const char F6SP[]="PAR";
const char F7SP[]="SER";   const char F8SP[]="OVY";      const char F9SP[]="HOR_REG";
const char F10SP[]="VER_REG"; const char F11SP[]="NORM_REG"; const char F12SP[]="IT";
const char F13SP[]="REP";  const char F14SP[]="DEF";     const char F15SP[]="NODE";
const char F16SP[]="MERGE"; const char F17SP[]="HEIGHT"; const char F18SP[]="WIDTH";
const char F19SP[]="DEPTH"; const char F20SP[]="BSIZE";  const char F21SP[]="SLAB";
const char F22SP[]="FIX";  const char F23SP[]="BCHAR";   const char F24SP[]="DUP";
const char F25SP[]="CC";   const char F26SP[]="EJECT";   const char F27SP[]="LRECL";
const char F28SP[]="LOC";
const char ABORCL_s[] = "ABORT";     const char CONTSP_s[] = "CONTINUE";
const char SCNTSP_s[] = "SCONTINUE"; const char FSSP[]   = "F";
const char KSP[]    = "K";           const char LSP[]    = "L";
const char RSP[]    = "R";           const char CSP[]    = "C";
const char VEESP[]  = "V";           const char TRLABP[] = "LABEL";
const char TRFRSP[] = "CALL";        const char TRKYSP[] = "KEYWORD";
const char STFCSP_kv[] = "STFCOUNT"; const char LSTNSP_kv[] = "LASTNO";
const char STCTSP_kv[] = "STCOUNT";  const char FNCLSP_kv[] = "FNCLEVEL";
const char DIGSP[]  = "DIGITS";

/* Pair list pair lists (trace name strings) */
const char TRLASP[] = "LABEL";

/* ── Control-card command SPEC_t globals (v311.sil §24 STRING directives) */
/* Each backed by a static char literal; .a/.l set in data_init()     */
static const char ctllit_UNLIST[]    = "UNLIST";
static const char ctllit_LIST[]      = "LIST";
static const char ctllit_EJECT[]     = "EJECT";
static const char ctllit_ERRORS[]    = "ERRORS";
static const char ctllit_NOERRORS[]  = "NOERRORS";
static const char ctllit_CASE[]      = "CASE";
static const char ctllit_INCLUDE[]   = "INCLUDE";
static const char ctllit_COPY[]      = "COPY";
static const char ctllit_PLUSOPS[]   = "PLUSOPS";
static const char ctllit_EXECUTE[]   = "EXECUTE";
static const char ctllit_NOEXECUTE[] = "NOEXECUTE";
static const char ctllit_LINE[]      = "LINE";
static const char ctllit_HIDE[]      = "HIDE";
static const char ctllit_LEFT[]      = "LEFT";

SPEC_t UNLSP_sp  = {0};
SPEC_t LISTSP_sp = {0};
SPEC_t EJCTSP_sp = {0};
SPEC_t ERORSP_sp = {0};
SPEC_t NERRSP_sp = {0};
SPEC_t CASESP_sp = {0};
SPEC_t INCLSP_sp = {0};
SPEC_t COPYSP_sp = {0};
SPEC_t SPITSP_sp = {0};
SPEC_t EXECSP_sp = {0};
SPEC_t NEXESP_sp = {0};
SPEC_t LINESP_sp = {0};
SPEC_t HIDESP_sp = {0};
SPEC_t LEFTSP_sp = {0};

/* ── data_init: initialize computed constants and stacks ─────────── */

void data_init(void)
{
    ARBSIZ.a.i = (int_t)(8 * NODESZ); /* Computed DESCR-valued constants — must be done at runtime  because DESCR is a compile-time sizeof, not a macro constant */
    CNDSIZ.a.i = (int_t)(CNODSZ); CNDSIZ.v = B;
    CODELT.a.i = (int_t)(200 * DESCR); CODELT.v = C;
    DSCRTW.a.i = (int_t)(2 * DESCR);
    ESALIM.a.i = (int_t)(ESASIZ * DESCR);
    EXTVAL.a.i = (int_t)(EXTSIZ * 2 * DESCR);
    FBLKRQ.a.i = (int_t)(FBLKSZ); FBLKRQ.v = B;
    INCLSZ.a.i = (int_t)(4 * DESCR); INCLSZ.v = B;
    IOBLSZ.a.i = (int_t)(2 * DESCR); IOBLSZ.v = B;
    LNODSZ.a.i = (int_t)(NODESZ + DESCR); LNODSZ.v = P;
    NODSIZ.a.i = (int_t)(NODESZ); NODSIZ.v = P;
    SNODSZ.a.i = (int_t)(NODESZ); SNODSZ.v = P;
    OCALIM.a.i = (int_t)(OCASIZ * DESCR); OCALIM.v = C;
    STARSZ.a.i = (int_t)(11 * DESCR); STARSZ.v = P;
    COMDCT.a.i = (int_t)(15 * DESCR);
    COMREG.a.i = P2A(&ELEMND);   /* SIL: COMREG DESCR ELEMND,0,0 — A=ELEMND */
    TRCBLK[0].a.i = P2A(TRCBLK); /* SIL: TRCBLK[0].a = self (block header self-ref) */
    TRSKEL.a.i = P2A(TRCBLK);    /* SIL: TRSKEL DESCR TRCBLK,0,0 — A=TRCBLK */
    /* LIT1CL: .a slots [0] and [2] = P2A(&LITFN) */
    LIT1CL[0].a.i = P2A(&LITFN);
    LIT1CL[2].a.i = P2A(&LITFN);
    /* TFNCLP: [0].a=TFENTL, [1].a=TRFRSP */
    TFNCLP[0].a.i = P2A(TFENTL);
    TFNCLP[1].a.i = P2A(TRFRSP);
    /* TFNRLP: 7 pairs per SIL [PLB115] */
    TFNRLP[0].a.i  = P2A(TFEXTL);  TFNRLP[1].a.i  = P2A(RETSP);
    TFNRLP[2].a.i  = P2A(TKEYL);   TFNRLP[3].a.i  = P2A(TRKYSP);
    TFNRLP[4].a.i  = P2A(TVALL);   TFNRLP[5].a.i  = P2A(VEESP);
    TFNRLP[6].a.i  = P2A(TLABL);   TFNRLP[7].a.i  = P2A(LSP);
    TFNRLP[8].a.i  = P2A(TFENTL);  TFNRLP[9].a.i  = P2A(CSP);
    TFNRLP[10].a.i = P2A(TFEXTL);  TFNRLP[11].a.i = P2A(RSP);
    TFNRLP[12].a.i = P2A(TKEYL);   TFNRLP[13].a.i = P2A(KSP);
    /* ATRHD: .a = P2A(ATPRCL) - DESCR (points one slot before prototype) */
    ATRHD.a.i = P2A(ATPRCL) - DESCR;
    SIZLMT.a.i = (int_t)(0x7fffffff);
    /* OBEND and OBPTR set by arena_init() — OBLIST now lives in arena */
    OUTBLK.a.i = P2A(OUTPUT) - DESCR;  /* OUTPUT is now array; OUTBLK = &OUTPUT[0] - DESCR */
    ERRBLK.a.i = P2A(&PUNCH) - DESCR;
    /* OTSATL: self-ref table header, body = 4*DESCR */
    OTSATL.a.i = P2A(&OTSATL); OTSATL.v = (int_t)(4 * DESCR);
    /* INSATL: self-ref table header, body = 2*DESCR */
    INSATL.a.i = P2A(&INSATL); INSATL.v = (int_t)(2 * DESCR);
    /* INLIST: self-ref table header, body = 4*DESCR [PLB36] */
    INLIST.a.i = P2A(&INLIST); INLIST.v = (int_t)(4 * DESCR);
    /* OTLIST: self-ref table header, body = 4*DESCR */
    OTLIST.a.i = P2A(&OTLIST); OTLIST.v = (int_t)(4 * DESCR);
    /* OUTPUT[1].a = OUTPSP string pointer */
    OUTPUT[1].a.i = P2A(OUTPSP);
    /* PCHFST.a = CRDFSP string pointer */
    PCHFST.a.i = P2A(CRDFSP);
    /* TRLIST: self-ref header + TVALL slot */
    TRLIST[0].a.i = P2A(TRLIST); TRLIST[0].v = (int_t)(20 * DESCR);
    TRLIST[1].a.i = P2A(TVALL);
    /* VALTRS: VALSP / TLABL / TRLASP */
    VALTRS[0].a.i = P2A(VALSP);
    VALTRS[1].a.i = P2A(TLABL);
    VALTRS[2].a.i = P2A(TRLASP);
    OPTBL.a.i  = P2A(&OPTBL);          /* OPTBL: self-referential table header (oracle: A=OPTBL) */
    OPTBL.f    = TTL|MARK;              /* oracle: F=TTL+MARK */
    OPTBL.v    = 0;                     /* oracle V=OPTBND-OPTBL-DESCR; Silly: non-contiguous, use 0 */
#define INIT_SP(sp, lit) (sp).a = P2A(lit); (sp).o = 0; (sp).l = (int32_t)(sizeof(lit)-1) /* Stacks — allocate from arena (done by arena_init before us)  pdl_stack and stack are arena-allocated; set arena offsets  Actual allocation is in arena_init(); we just record the pointers  Control-card command SPEC_t globals — backed by static literals */
    INIT_SP(UNLSP_sp, ctllit_UNLIST);
    INIT_SP(LISTSP_sp, ctllit_LIST);
    INIT_SP(EJCTSP_sp, ctllit_EJECT);
    INIT_SP(ERORSP_sp, ctllit_ERRORS);
    INIT_SP(NERRSP_sp, ctllit_NOERRORS);
    INIT_SP(CASESP_sp, ctllit_CASE);
    INIT_SP(INCLSP_sp, ctllit_INCLUDE);
    INIT_SP(COPYSP_sp, ctllit_COPY);
    INIT_SP(SPITSP_sp, ctllit_PLUSOPS);
    INIT_SP(EXECSP_sp, ctllit_EXECUTE);
    INIT_SP(NEXESP_sp, ctllit_NOEXECUTE);
    INIT_SP(LINESP_sp, ctllit_LINE);
    INIT_SP(HIDESP_sp, ctllit_HIDE);
    INIT_SP(LEFTSP_sp, ctllit_LEFT);
#undef INIT_SP
    /* EXDTSP STRING 'EXTERNAL' — v311.sil line 10910 */
    { static const char exdt_lit[] = "EXTERNAL";
      EXDTSP.a = P2A(exdt_lit); EXDTSP.o = 0;
      EXDTSP.l = (int32_t)(sizeof(exdt_lit)-1); EXDTSP.v = S; }
    /* ALPHSP SPEC — v311.sil line 10883 */
    { static const char alph_lit[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
      ALPHSP.a = P2A(alph_lit); ALPHSP.o = 0;
      ALPHSP.l = (int32_t)(sizeof(alph_lit)-1); ALPHSP.v = S; }
    /* Register all platform scan tables (CARDTB, IBLKTB, ELEMTB, etc.)
     * Must be called after arena_init() sets arena_base. */
    extern void init_syntab(void);
    init_syntab();
}
