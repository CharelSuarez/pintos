#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>

struct frame {
    void* frame;
    struct list_elem frames_elem;
};

void frame_init(void);
struct frame* frame_allocate(void);
struct frame* frame_allocate_zeros(void);
void frame_free(struct frame* frame);
void frame_free_all(void);

#endif /* vm/frame.h */
