#include "vm/frame.h"
#include "threads/palloc.h"
#include <debug.h>
#include "threads/malloc.h"
#include <stdbool.h>

static struct list frames;

static struct frame* _frame_allocate(bool zeros);

void frame_init() {
    list_init(&frames);
}

struct frame* frame_allocate() {
    return _frame_allocate(false);
}

struct frame* frame_allocate_zeros() {
    return _frame_allocate(true);
}

static struct frame* _frame_allocate(bool zeros) {
    void* page = palloc_get_page(PAL_USER | (zeros ? PAL_ZERO : 0));

    if (!page) {
        PANIC("No pages available to allocate!"); // TODO Error handling!
    }

    struct frame* new_frame = malloc(sizeof(struct frame));
    new_frame->frame = page;
    list_push_back(&frames, &new_frame->frames_elem);

    return new_frame;
}

void frame_free(struct frame* frame) {
    list_remove(&frame->frames_elem);
    free(frame);
}

void frame_free_all() {
    while (!list_empty(&frames)) {
        struct frame* frame = list_entry(list_front(&frames), 
                                         struct frame, frames_elem);
        frame_free(frame);
    }
}