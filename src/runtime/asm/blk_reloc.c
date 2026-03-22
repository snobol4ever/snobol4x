/*
 * blk_reloc.c — relocation patcher for copied Byrd-box CODE blocks
 *
 * The emitter records all PC-relative and absolute references inside each
 * box's code in NASM data (box_N_reloc_table / box_N_reloc_count).  At
 * runtime, after memcpy-ing a CODE block to a new address, call
 * blk_relocate() to fix up those references before making the block RX.
 */
#include <stdint.h>
#include <stddef.h>
#include "blk_reloc.h"

/*
 * blk_relocate — apply all relocation entries to a copied TEXT block.
 *
 * For BLK_RELOC_REL32:
 *   The 4-byte field at text+offset holds a displacement relative to the
 *   original base.  We adjust it by -delta so it remains correct at the
 *   new address.  (new_disp = old_disp - delta)
 *
 * For BLK_RELOC_ABS64:
 *   The 8-byte field at text+offset holds an absolute address.  We add
 *   delta to it.  (new_abs = old_abs + delta)
 *
 * Returns 0 on success, -1 if any entry is out of bounds.
 */
int blk_relocate(uint8_t *text, size_t len,
                 ptrdiff_t delta,
                 const blk_reloc_entry *table, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t off  = table[i].offset;
        blk_reloc_kind kind = table[i].kind;

        switch (kind) {
        case BLK_RELOC_REL32: {
            if (off + 4 > len) return -1;
            int32_t disp;
            __builtin_memcpy(&disp, text + off, 4);
            disp -= (int32_t)delta;
            __builtin_memcpy(text + off, &disp, 4);
            break;
        }
        case BLK_RELOC_ABS64: {
            if (off + 8 > len) return -1;
            uint64_t addr;
            __builtin_memcpy(&addr, text + off, 8);
            addr += (uint64_t)delta;
            __builtin_memcpy(text + off, &addr, 8);
            break;
        }
        default:
            return -1;
        }
    }
    return 0;
}
