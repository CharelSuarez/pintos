#include "vm/swap.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

#define SECTORS_NEEDED PGSIZE/BLOCK_SECTOR_SIZE

struct block* swap;
static void* buffer;
static block_sector_t explored_sector = 0;
static block_sector_t free_sector = 0;
static struct lock swap_lock;

void
swap_init() {
    swap = block_get_role(BLOCK_SWAP);
    buffer = palloc_get_page(0);
    lock_init(&swap_lock);
}

block_sector_t 
swap_write(struct frame* frame) {
    lock_acquire(&swap_lock);
    if (free_sector + SECTORS_NEEDED >= block_size(swap)) {
        PANIC("Out of swap memory!");
    }

    block_sector_t sector = explored_sector;
    // If a sector was freed, use it first.
    if (free_sector < explored_sector) {
        sector = free_sector;
        // Read the next free sector from the free block.
        block_read(swap, free_sector, buffer);
        free_sector = *((block_sector_t*) buffer);
    } else {
        // If the next free sector is the farthest explored, keep exploring.
        free_sector += SECTORS_NEEDED;
        explored_sector += SECTORS_NEEDED;
    }

    for (size_t i = 0; i < SECTORS_NEEDED; i++) {
        block_write(swap, sector + i, frame->frame + BLOCK_SECTOR_SIZE * i);
    }

    lock_release(&swap_lock);
    return sector;
}

/* Allocates a new frame and reads the data from the given swap sector 
   into the frame. */
struct frame* 
swap_read(block_sector_t sector) {
    // Note: This frame can't be de-allocated until placed into a page.
    struct frame* frame = frame_allocate();
    if (!frame) {
        return NULL;
    }
    lock_acquire(&swap_lock);
    for (size_t i = 0; i < SECTORS_NEEDED; i++) {
        block_read(swap, sector + i, frame->frame + BLOCK_SECTOR_SIZE * i);
    }

    lock_release(&swap_lock);
    swap_free(sector);
    return frame;
}

void 
swap_free(block_sector_t sector) {
    lock_acquire(&swap_lock);
    *((block_sector_t*) buffer) = free_sector;
    block_write(swap, sector, buffer);

    free_sector = sector;
    lock_release(&swap_lock);
}