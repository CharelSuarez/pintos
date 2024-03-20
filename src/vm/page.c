#include "vm/page.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include "vm/swap.h"

static struct page* _page_create(void* vaddr, struct frame* frame, 
                                 bool writable); 
static bool _page_insert(struct page* page);
static void _page_set_frame(struct page* page, struct frame* frame);
static void _page_free(struct page* page, bool delete);

static unsigned page_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool page_less_func(const struct hash_elem *_a, 
                          const struct hash_elem *_b, void *aux UNUSED);
                          
void 
page_init(struct thread* thread) {
    hash_init(&thread->pages, page_hash_func, page_less_func, NULL);
}

void* 
page_create(void* vaddr, bool zeros, bool writable) {
    struct frame* frame = zeros ? frame_allocate_zeros() : frame_allocate();
    if (!frame) {
        return NULL;
    }
    struct page* page = _page_create(vaddr, frame, writable);
    if (!page) {
        frame_free(frame);
        return NULL;
    }
    return frame->frame;
}

struct page* 
page_create_mmap(void* vaddr, struct file* file, off_t offset, 
                              size_t length) {
    struct page* page = _page_create(vaddr, NULL, true);
    if (!page) {
        return NULL;
    }
    page->type = PAGE_MMAP;
    page->file = file;
    page->offset = offset;
    page->length = length;
    return page;
}

struct page* 
page_create_executable(void* vaddr, struct file* file, 
        off_t offset, size_t length, bool writable) {
    struct page* page = _page_create(vaddr, NULL, writable);
    if (!page) {
        return NULL;
    }
    page->type = PAGE_EXECUTABLE;
    page->file = file;
    page->offset = offset;
    page->length = length;
    return page;
}

static struct page* 
_page_create(void* vaddr, struct frame* frame, 
                                 bool writable) {
    struct page* page = malloc(sizeof(struct page));
    if (!page) {
        return NULL;
    }
    page->vaddr = pg_round_down(vaddr);
    page->thread = thread_current();
    page->writable = writable;
    page->type = PAGE_NORMAL;
    page->file = NULL;
    page->offset = 0;
    page->swapped = false;
    // Magics for debug.
    page->swap_sector = 69;
    page->length = 69;
    if (!_page_insert(page)) {
        free(page);
        return NULL;
    }
    _page_set_frame(page, frame);
    return page;
}

static bool 
_page_insert(struct page* page) {
    struct thread* t = page->thread;
    // Can't insert page on top of existing page!
    if (hash_find(&t->pages, &page->pages_elem) != NULL) {
        return false;
    }
    hash_insert(&t->pages, &page->pages_elem);
    return true;
}

void 
page_free(struct page* page) {
    _page_free(page, true);
}

static void 
_page_free(struct page* page, bool delete) {
    if (page->swapped) {
        swap_free(page->swap_sector);
    }
    if (page->frame) {
        frame_free(page->frame);
    }
    if (delete) {
        hash_delete(&page->thread->pages, &page->pages_elem);
    }
    free(page);
}

struct 
page* page_find(void* vaddr) {
    void* page_vaddr = pg_round_down(vaddr);
    struct page find_page;
    find_page.vaddr = page_vaddr;

    struct thread* t = thread_current();
    struct hash_elem* page = hash_find(&t->pages, &find_page.pages_elem);
    if (!page) {
        return NULL;
    }
    return hash_entry(page, struct page, pages_elem);
}

bool 
page_try_load_in_frame(struct page* page) {
    // If page already has a frame, there's no loading to be done.
    if (page->frame) {
        return false;
    }
    if (page->swapped) {
        struct frame* frame = swap_read(page->swap_sector);
        if (!frame) {
            return false;
        }
        // Data that is swapped in is considered dirty.
        pagedir_set_dirty(page->thread->pagedir, page->vaddr, true);
        _page_set_frame(page, frame);
        return true;
    }
    if (true) {
        struct frame* frame = frame_allocate();
        if (!frame) {
            return false;
        }
        if (page->type == PAGE_MMAP || page->type == PAGE_EXECUTABLE) {
            size_t length = page->length;
            if (length > 0) {
                struct file* file = page->file;
                file_read_at(file, frame->frame, length, page->offset);
            }
            if (length < PGSIZE) {
                memset(frame->frame + length, 0, PGSIZE - length);
            }
        }
        _page_set_frame(page, frame);
        return true;
    }
    return false;
}

/* Assigns the given frame to the given page, and mapping from virtual 
   page to frame is created. The page is set to be out of swap memory, 
   and the frame to unlocked state.

   NOTE: This means after this call, the frame may be instantly 
   de-allocated. */
static void 
_page_set_frame(struct page* page, struct frame* frame) {
    page->frame = frame;
    if (frame) {
        page->swapped = false;
        // Add the mapping from virtual page to kernel page.
        pagedir_set_page(page->thread->pagedir, page->vaddr, 
                         frame->frame, page->writable);
        frame->page = page; // Allows the frame to de-allocated.
    }
}

static unsigned 
page_hash_func(const struct hash_elem *e, void *aux UNUSED) {
  struct page *page = hash_entry(e, struct page, pages_elem);
  return hash_int((int) page->vaddr);
}

static bool 
page_less_func(const struct hash_elem *_a, const struct hash_elem *_b, 
               void *aux UNUSED) {
  struct page *a = hash_entry(_a, struct page, pages_elem);
  struct page *b = hash_entry(_b, struct page, pages_elem);
  return a->vaddr < b->vaddr;
}

void 
page_destroy(struct hash_elem *e, void *aux UNUSED) {
    struct page *page = hash_entry(e, struct page, pages_elem);
    _page_free(page, false);
}
