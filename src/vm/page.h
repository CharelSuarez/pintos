#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/thread.h"

struct page {
    void* vaddr;
    struct frame* frame;
    bool writable;

    struct hash_elem pages_elem;
};

void page_init(struct thread* thread); 

void* page_create(void* vaddr, bool zeros, bool writable);

void* page_create_with_frame(void* vaddr, struct frame* frame, bool writable);

void page_insert(struct page* page);

void page_remove(struct page* page);

struct page* page_find(void* vaddr);

#endif /* vm/page.h */
