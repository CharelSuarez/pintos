#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "devices/block.h"

/* The type of data that this page represents. */
typedef enum page_type {
    PAGE_NORMAL,      /* Misc user page type. */
    PAGE_MMAP,        /* Memory mapped file page type. */
    PAGE_EXECUTABLE,  /* Executable file page type. */
} page_type;

/* Represents a page in virtual user memory. */
struct page {
    void* vaddr;                 /* The virtual address of this page. */
    struct frame* frame;         /* The frame that this page is loaded into. */
    bool writable;               /* Whether this page is writable. */
    page_type type;              /* The type of data for this page. */
    struct thread* thread;       /* The owning thread of this page. */

    // Mmap / Executable Pages
    struct file* file;           /* The file that this page loads. */
    off_t offset;                /* The offset in the file to load from. */
    size_t length;               /* The size of data to read from the file.*/

    // Swapped Pages
    bool swapped;                /* Whether this page's frame is in swap. */
    block_sector_t swap_sector;  /* The swap sector the frame is stored in. */

    struct hash_elem pages_elem; /* The hash elem for thread pages list. */
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
