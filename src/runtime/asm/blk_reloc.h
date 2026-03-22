/*
 * blk_reloc.h — relocation table types and patcher API
 *
 * When a Byrd-box CODE block is copied to a new address, relative jumps
 * and absolute DATA references must be patched.  The emitter records all
 * such references in a per-box relocation table (NASM .rodata):
 *   box_N_reloc_table   — array of blk_reloc_entry
 *   box_N_reloc_count   — number of entries
 *
 * Relocation kinds:
 *   BLK_RELOC_REL32  — 32-bit PC-relative offset (e.g. jmp/call near)
 *   BLK_RELOC_ABS64  — 64-bit absolute pointer (e.g. lea rax,[DATA+off])
 */
#ifndef BLK_RELOC_H
#define BLK_RELOC_H
#include <stdint.h>
#include <stddef.h>

typedef enum {
    BLK_RELOC_REL32 = 1,   /* 32-bit signed PC-relative displacement */
    BLK_RELOC_ABS64 = 2    /* 64-bit absolute address */
} blk_reloc_kind;

typedef struct {
    uint32_t        offset; /* byte offset within the block */
    blk_reloc_kind  kind;
} blk_reloc_entry;

/*
 * blk_relocate — patch all relocations in a copied TEXT block.
 *
 * text   — writable copy of the CODE block (output of memcpy)
 * len    — byte length of the block
 * delta  — (new_addr - old_addr), i.e. how far the block moved
 * table  — relocation entries from box_N_reloc_table
 * n      — number of entries (box_N_reloc_count)
 *
 * Call BEFORE blk_mprotect_rx.
 */
int blk_relocate(uint8_t *text, size_t len,
                 ptrdiff_t delta,
                 const blk_reloc_entry *table, size_t n);

#endif /* BLK_RELOC_H */
