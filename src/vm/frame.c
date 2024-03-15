#include "vm/frame.h"
#include "threads/palloc.h"
#include <debug.h>
#include "threads/malloc.h"
#include <stdbool.h>
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "lib/stdio.h"

static struct list frames;
static size_t frame_count;

static struct frame* _frame_allocate(bool zeros);
static struct frame* evict_frame(void);

void frame_init() {
    list_init(&frames);
}

struct frame* frame_allocate() {
    return _frame_allocate(false);
}

struct frame* frame_allocate_zeros() {
    return _frame_allocate(true); 
}

void frame_set_page(struct frame* frame, struct page* vpage) {
    frame->page = vpage;
}

static struct frame* _frame_allocate(bool zeros) {
    struct frame* new_frame = NULL;
    void* page = palloc_get_page(PAL_USER | (zeros ? PAL_ZERO : 0));
    if (!page) {
        // This town ain't big enough for the both of us >:(
        new_frame = evict_frame();
        if (!new_frame) {
            // No frames available to evict!
            return NULL;
        }
        page = palloc_get_page(PAL_USER | (zeros ? PAL_ZERO : 0));
        if (!page) {
            // No pages available after eviction!
            return NULL;
        }
    }

    if (!new_frame) {
        new_frame = malloc(sizeof(struct frame));
        if (!new_frame) {
            // Failed to allocate a new frame!
            palloc_free_page(page); // TODO Is this needed?
            return NULL;
        }
    }
    new_frame->frame = page;
    list_push_back(&frames, &new_frame->frames_elem);
    frame_count++;

    return new_frame;
}

static struct frame* evict_frame() {
    size_t count = 0;
    struct thread* t = thread_current();
    while (count < frame_count) {
        struct frame* first = list_entry(list_front(&frames), 
                                         struct frame, frames_elem);
        // If frame was accessed, reset and push back.
        if (pagedir_is_accessed(t->pagedir, first->frame)) {
            pagedir_set_accessed(t->pagedir, first->frame, false);
            list_remove(&first->frames_elem);
            list_push_back(&frames, &first->frames_elem);
        } else {
            palloc_free_page(first->frame); // Remove from (real) file table.
            frame_free(first);
            return first;
        }
        count++;
    }
    return NULL;
}

void frame_free(struct frame* frame) {
    list_remove(&frame->frames_elem);
    frame_count--;
    free(frame);
    if (frame->page) {
        frame->page->frame = NULL;
    }
}

void frame_free_all() {
    while (!list_empty(&frames)) {
        struct frame* frame = list_entry(list_front(&frames), 
                                         struct frame, frames_elem);
        frame_free(frame);
    }
}