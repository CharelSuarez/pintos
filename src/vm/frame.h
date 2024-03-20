#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "vm/page.h"

/* Represents a physical frame. */
struct frame {
    void* frame;        /* The kernel page address for this frame. */
    struct page* page;  /* The virtual page that is mapped to this frame.*/

    struct list_elem frames_elem; /* The elem for the global frame list. */
};

void frame_init(void);
struct frame* frame_allocate(void);
struct frame* frame_allocate_zeros(void);
void frame_free(struct frame* frame);
void frame_free_all(void);

#endif /* vm/frame.h */
