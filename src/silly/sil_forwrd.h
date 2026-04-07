/*
 * sil_forwrd.h — Text scanning / card processing (v311.sil §4+§6)
 *
 * CODSKP  — skip N object-code items   (§4 line 1116, pure C)
 * FORWRD  — advance to next character   (§6 line 2214, uses STREAM)
 * FORBLK  — advance to nonblank         (§6 line 2241, uses STREAM)
 * NEWCRD  — process new card image      (§6 line 2272, uses STREAM)
 * FILCHK  — handle EOF / include pop    (§6 line 2350)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18b
 */

#ifndef SIL_FORWRD_H
#define SIL_FORWRD_H

#include "sil_types.h"

/* CODSKP — skip n items in current object-code stream.
 * v311.sil CODSKP line 1116. */
void CODSKP_fn(int32_t n);

/* FORWRD — advance TEXTSP to next non-whitespace character.
 * Sets BRTYPE. Returns OK on success, FAIL on end-of-input. */
RESULT_t FORWRD_fn(void);

/* FORBLK — advance TEXTSP past blanks to nonblank.
 * Alias for the FORBLK label in v311.sil (calls STREAM IBLKTB). */
RESULT_t FORBLK_fn(void);

/* NEWCRD — process card type (comment/continue/control/normal).
 * Called by FORRUN after reading a new line. */
RESULT_t NEWCRD_fn(void);

/* CTLADV — advance to quoted filename arg on a control card (line 2430). */
RESULT_t CTLADV_fn(SPEC_t *out);

/* FILCHK — handle EOF in compilation (include-stack pop or file change). */
RESULT_t FILCHK_fn(void);

#endif /* SIL_FORWRD_H */
