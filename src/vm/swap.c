#include "vm/swap.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

#define SECTORS_NEEDED PGSIZE/BLOCK_SECTOR_SIZE

struct block* swap;
static void* buffer;
static block_sector_t explored_sector = 0;
static block_sector_t free_sector = 0;

void
swap_init() {
    swap = block_get_role(BLOCK_SWAP);
    buffer = palloc_get_page(0);
}

block_sector_t 
swap_write(struct frame* frame) {
    swap = block_get_role(BLOCK_SWAP);
    if (free_sector + SECTORS_NEEDED - 1 >= block_size(swap)) {
        PANIC("Out of swap memory!");
    }

    block_sector_t sector = explored_sector;
    if (explored_sector + SECTORS_NEEDED - 1 >= block_size(swap)) {
        // Explored every sector, time to use free_sector.
        sector = free_sector;
        // Next free sector is stored in the free block.
        block_read(swap, free_sector, buffer);
        free_sector = *((block_sector_t*) buffer);
    } else {
        free_sector += SECTORS_NEEDED;
        explored_sector += SECTORS_NEEDED;
    }

    for (size_t i = 0; i < SECTORS_NEEDED; i++) {
        block_write(swap, sector + i, frame->frame + BLOCK_SECTOR_SIZE * i);
    }
    
    return sector;
}

struct frame* 
swap_read(block_sector_t sector) {
    swap = block_get_role(BLOCK_SWAP);
    struct frame* frame = frame_allocate();
    if (!frame) {
        return NULL;
    }

    for (size_t i = 0; i < SECTORS_NEEDED; i++) {
        block_read(swap, sector + i, frame->frame + BLOCK_SECTOR_SIZE * i);
    }

    *((block_sector_t*) buffer) = free_sector;
    block_write(swap, sector, buffer);
    free_sector = sector;
    return frame;
}