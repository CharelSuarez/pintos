#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/frame.h"
#include "devices/block.h"

void swap_init(void);
block_sector_t swap_in_frame(struct frame* frame);
struct frame* swap_out_frame(block_sector_t sector);

#endif /* vm/swap.h */