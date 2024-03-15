#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/file.h"

typedef enum page_type {
    PAGE_NONE,
    PAGE_MMAP,
    PAGE_FILE,
} page_type;

struct page {
    void* vaddr;
    struct frame* frame;
    bool writable;
    page_type type;

    // MMAP
    struct file* file;
    off_t offset;

    struct hash_elem pages_elem;
};

void page_init(struct thread* thread); 

void* page_create(void* vaddr, bool zeros, bool writable);
void* page_create_with_frame(void* vaddr, struct frame* frame, bool writable);
struct page* page_create_mmap(void* vaddr, struct file* file, off_t offset);

void page_insert(struct page* page);

void page_remove(struct page* page);

struct page* page_find(void* vaddr);

#endif /* vm/page.h */
