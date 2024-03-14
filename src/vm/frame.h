#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>

struct frame {
    void* frame;
    struct list_elem frames_elem;
};

void init_frame(void);
struct frame* allocate_frame(void);
struct frame* allocate_frame_zeros(void);
void free_frame(struct frame* frame);
void free_all_frames(void);

#endif /* vm/frame.h */
