#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "devices/block.h"

typedef enum page_type {
    PAGE_NORMAL,
    PAGE_MMAP,
    PAGE_EXECUTABLE,
} page_type;

struct page {
    void* vaddr;
    struct frame* frame;
    bool writable;
    page_type type;
    struct thread* thread;

    // Mmap / Executable Pages
    struct file* file;
    off_t offset;
    size_t length;

    // Swapped Pages
    bool swapped;
    block_sector_t swap_sector;

    struct hash_elem pages_elem;
};

void page_init(struct thread* thread); 
void* page_create(void* vaddr, bool zeros, bool writable);
struct page* page_create_mmap(void* vaddr, struct file* file, off_t offset, 
                              size_t length);
struct page* page_create_executable(void* vaddr, struct file* file, 
        off_t offset, size_t length, bool writable);
struct page* page_find(void* vaddr);
bool page_try_load_in_frame(struct page* page);
void page_free(struct page* page);
void page_destroy(struct hash_elem *e, void *aux UNUSED);

#endif /* vm/page.h */
