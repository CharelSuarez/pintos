#include "vm/frame.h"
#include "threads/palloc.h"
#include <debug.h>
#include "threads/malloc.h"
#include <list.h>
#include <stdbool.h>

static struct list frames;

static struct frame* _allocate_frame(bool zeros);

void init_frame() {
    list_init(&frames);    
}

struct frame* allocate_frame() {
    return _allocate_frame(false);
}

struct frame* allocate_frame_zeros() {
    return _allocate_frame(true);
}

static struct frame* _allocate_frame(bool zeros) {
    void* page = palloc_get_page(PAL_USER);

    if (!page) {
        PANIC("No pages available to allocate!");
    }

    struct frame* new_frame = zeros ? 
        calloc(sizeof(struct frame), 1) : malloc(sizeof(struct frame));
    new_frame->frame = page;

    return new_frame;
}

void free_frame(struct frame* frame) {
    list_remove(&frame->frames_elem);
    free(frame);
}

void free_all_frames() {
    while (!list_empty(&frames)) {
        struct frame* frame = list_entry(list_front(&frames), 
                                         struct frame, frames_elem);
        free_frame(frame);
    }
}