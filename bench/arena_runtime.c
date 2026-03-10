#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "runtime.h"

/* Arena allocator — reset between matches, zero malloc overhead */
static char  _arena[4096];
static size_t _arena_pos = 0;

void sno_arena_reset(void) { _arena_pos = 0; }

typedef struct frame_header {
    struct frame_header *prev;
    size_t size;
} frame_header_t;

void *sno_enter(void **frame_ptr, size_t frame_size) {
    size_t total = sizeof(frame_header_t) + frame_size;
    /* align to 8 bytes */
    total = (total + 7) & ~7ULL;
    if (_arena_pos + total > sizeof(_arena)) {
        fprintf(stderr, "arena overflow\n"); exit(1);
    }
    frame_header_t *hdr = (frame_header_t *)(_arena + _arena_pos);
    _arena_pos += total;
    hdr->prev  = (frame_header_t *)*frame_ptr;
    hdr->size  = frame_size;
    void *frame = (void *)(hdr + 1);
    memset(frame, 0, frame_size);
    *frame_ptr = frame;
    return frame;
}

void sno_exit(void **frame_ptr) {
    if (!*frame_ptr) return;
    frame_header_t *hdr = ((frame_header_t *)*frame_ptr) - 1;
    *frame_ptr = (void *)hdr->prev;
    _arena_pos -= (hdr->size + sizeof(frame_header_t) + 7) & ~7ULL;
}
