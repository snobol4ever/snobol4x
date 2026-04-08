/*
 * sil_errors.h — Error handlers and termination (v311.sil §22+§23)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M20
 */

#ifndef SIL_ERRORS_H
#define SIL_ERRORS_H

#include "types.h"

/* Fatal termination paths */
void FTLEND_fn(void);   /* fatal end (always terminates)                 */
void FTERST_fn(void);   /* core restart: check ERRLIMIT, ERRTEXT, XITHND */
void FTLTST_fn(void);   /* non-fatal: FATLCL=0, fall to FTERST          */
void FTLERR_fn(void);   /* check &FATALLIMIT, FATLCL=1, fall to FTERST  */
void END_fn(void);      /* normal program end                            */
void SYSCUT_fn(void);   /* signal/interrupt cut                          */

/* Error entry points — each sets ERRTYP then calls FTLTST/FTLERR/FTLEND */
void AERROR_fn(void);   /* arithmetic error             ERRTYP=2        */
void ALOC2_fn(void);    /* storage exhausted            ERRTYP=20       */
void ARGNER_fn(void);   /* wrong argument count         ERRTYP=25       */
void CFTERR_fn(void);   /* cannot CONTINUE from FATAL   ERRTYP=39       */
void CNTERR_fn(void);   /* not in SETEXIT handler       ERRTYP=35       */
void COMP1_fn(void);    /* missing END                  ERRTYP=32       */
void COMP3_fn(void);    /* program error                ERRTYP=17       */
void COMP5_fn(void);    /* reading error                ERRTYP=11       */
void COMP6_fn(void);    /* writing error                ERRTYP=33       */
void COMP7_fn(void);    /* erroneous END                ERRTYP=27       */
void COMP9_fn(void);    /* compilation error limit      ERRTYP=26       */
void EROR_fn(void);     /* erroneous statement (inserts stno)           */
void EXEX_fn(void);     /* &STLIMIT exceeded            ERRTYP=22       */
void INTR1_fn(void);    /* illegal data type            ERRTYP=1        */
void INTR4_fn(void);    /* erroneous goto               ERRTYP=24       */
void INTR5_fn(void);    /* failure in goto              ERRTYP=19       */
void INTR8_fn(void);    /* exceeded &MAXLNGTH           ERRTYP=15       */
void INTR10_fn(void);   /* program error (= COMP3)      ERRTYP=17       */
void INTR13_fn(void);   /* program error (= COMP3)      ERRTYP=17       */
void INTR27_fn(void);   /* excessive data types         ERRTYP=13       */
void INTR30_fn(void);   /* illegal argument             ERRTYP=10       */
void INTR31_fn(void);   /* pattern stack overflow       ERRTYP=16       */
void LENERR_fn(void);   /* negative number              ERRTYP=14       */
void MAIN1_fn(void);    /* return from level zero       ERRTYP=18       */
void NEMO_fn(void);     /* variable not present         ERRTYP=8        */
void NONAME_fn(void);   /* null string                  ERRTYP=4        */
void NONARY_fn(void);   /* bad array/table ref          ERRTYP=3        */
void OVER_fn(void);     /* stack overflow                ERRTYP=21      */
void PROTER_fn(void);   /* prototype error              ERRTYP=6        */
void SCERST_fn(void);   /* scan error restart           SCERCL=1        */
void SIZERR_fn(void);   /* object too large             ERRTYP=23       */
void UNDF_fn(void);     /* undefined function           ERRTYP=5        */
void UNDFFE_fn(void);   /* fn entry point not label     ERRTYP=9        */
void UNKNKW_fn(void);   /* unknown keyword              ERRTYP=7        */
void UNTERR_fn(void);   /* illegal I/O unit             ERRTYP=12       */
void USRINT_fn(void);   /* user interrupt (SIGINT)      ERRTYP=34       */

#endif /* SIL_ERRORS_H */
