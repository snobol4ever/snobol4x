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
SIL_result FORWRD_fn(void);

/* FORBLK — advance TEXTSP past blanks to nonblank.
 * Alias for the FORBLK label in v311.sil (calls STREAM IBLKTB). */
SIL_result FORBLK_fn(void);

/* NEWCRD — process card type (comment/continue/control/normal).
 * Called by FORRUN after reading a new line. */
SIL_result NEWCRD_fn(void);

/* FILCHK — handle EOF in compilation (include-stack pop or file change). */
SIL_result FILCHK_fn(void);

#endif /* SIL_FORWRD_H */
