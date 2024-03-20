#include "vm/frame.h"
#include "threads/palloc.h"
#include <debug.h>
#include "threads/malloc.h"
#include <stdbool.h>
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "lib/stdio.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "userprog/process.h"

/* The list of all kernel frames. */
static struct list frames;
/* The count of frames in the frame list. */
static size_t frame_count;
/* Aquired whenever the frame list is modified. */
static struct lock frame_lock;

static struct frame* _frame_allocate(bool zeros);
static bool _evict_frame(void);
static void _frame_free(struct frame* frame, bool lock_owned); 

void frame_init() {
    list_init(&frames);
    lock_init(&frame_lock);
    frame_count = 0;
}

struct frame* frame_allocate() {
    return _frame_allocate(false);
}

struct frame* frame_allocate_zeros() {
    return _frame_allocate(true); 
}

static struct frame* _frame_allocate(bool zeros) {
    lock_acquire(&frame_lock);
    struct frame* new_frame = NULL;
    void* page = palloc_get_page(PAL_USER | (zeros ? PAL_ZERO : 0));
    if (!page) {
        // This town ain't big enough for the both of us >:(
        if (!_evict_frame()) {
            lock_release(&frame_lock);
            return NULL;
        }
        page = palloc_get_page(PAL_USER | (zeros ? PAL_ZERO : 0));
        if (!page) {
            lock_release(&frame_lock);
            return NULL;
        }
    }

    if (!new_frame) {
        new_frame = calloc(sizeof(struct frame), 1);
        if (!new_frame) {
            palloc_free_page(page);
            lock_release(&frame_lock);
            return NULL;
        }
    }
    new_frame->page = NULL;
    new_frame->frame = page;
    list_push_back(&frames, &new_frame->frames_elem);
    frame_count++;

    lock_release(&frame_lock);
    return new_frame;
}

static bool _evict_frame() {
    ASSERT (lock_held_by_current_thread(&frame_lock));

    size_t count = 0;
    size_t length = frame_count;
    while (count < length + 1) {
        struct frame* first = list_entry(list_front(&frames), 
                                         struct frame, frames_elem);
        struct page* page = first->page;
        if (page) {
            struct thread* t = page->thread;
            if (!pagedir_is_accessed(t->pagedir, page->vaddr)) {
                // Put the frame in swap and free it.
                if (page->type != PAGE_MMAP && 
                        (pagedir_is_dirty(t->pagedir, page->vaddr) ||
                        pagedir_is_dirty(t->pagedir, first->frame))) {
                    page->swap_sector = swap_write(first);
                    page->swapped = true;
                }
                _frame_free(first, true); 
                return true;
            }
            // Reset accessed state.
            pagedir_set_accessed(t->pagedir, page->vaddr, false);
        }
        // Push back frame.
        list_remove(&first->frames_elem);
        list_push_back(&frames, &first->frames_elem);
        count++;
    }
    PANIC("Failed to evict a frame!");
    return false;
}

void frame_free(struct frame* frame) {
    _frame_free(frame, false);
}

static void 
_frame_free(struct frame* frame, bool lock_owned) {
    if (!lock_owned) {
        lock_acquire(&frame_lock);
    } else {
        ASSERT(lock_held_by_current_thread(&frame_lock));
    }
    frame_count--;
    list_remove(&frame->frames_elem);
    struct page* page = frame->page;
    if (page) {
        if (page->type == PAGE_MMAP) {
            process_mmap_write_to_disk(page);
        }
        page->frame = NULL;
        // Remove from (real) file table.
        pagedir_clear_page(page->thread->pagedir, page->vaddr);
    }
    palloc_free_page(frame->frame);
    free(frame);
    if (!lock_owned) {
        lock_release(&frame_lock);
    }
}

void frame_free_all() {
    while (!list_empty(&frames)) {
        struct frame* frame = list_entry(list_front(&frames), 
                                         struct frame, frames_elem);
        frame_free(frame);
    }
}