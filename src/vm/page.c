#include "vm/page.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

static bool install_page (void *upage, void *kpage, bool writable);

static unsigned page_hash_func(const struct hash_elem *e, void *aux UNUSED);

static bool page_less_func(const struct hash_elem *_a, 
                          const struct hash_elem *_b, void *aux UNUSED);

void page_init(struct thread* thread) {
    hash_init(&thread->pages, page_hash_func, page_less_func, NULL);
}

bool page_create(void* vaddr, bool writable) {
    ASSERT (page_find(vaddr) == NULL)

    struct frame* frame = frame_allocate();
    if (!frame) {
        return false;
    }

    void* page_vaddr = pg_round_down(vaddr);
    struct page* page = malloc(sizeof(struct page));
    page->vaddr = page_vaddr;
    page->frame = frame;
    page->writable = writable;
    page_insert(page);
    return true;
}

void page_insert(struct page* page) {
    struct thread* t = thread_current();
    hash_insert(&t->pages, &page->pages_elem);
    install_page(page->vaddr, page->frame, page->writable);
}

void page_remove(struct page* page) {
    struct thread* t = thread_current();
    hash_delete(&t->pages, &page->pages_elem);
    free(page);
}

struct page* page_find(void* vaddr) {
    struct thread* t = thread_current();

    void* page_vaddr = pg_round_down(vaddr);
    struct page find_page;
    find_page.vaddr = page_vaddr;

    struct hash_elem* page = hash_find(&t->pages, &find_page.pages_elem);
    if (!page) {
        return NULL;
    }
    return hash_entry(page, struct page, pages_elem);
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

static unsigned page_hash_func(const struct hash_elem *e, void *aux UNUSED) {
  struct page *page = hash_entry(e, struct page, pages_elem);
  return hash_int((int) page->vaddr);
}

static bool page_less_func(const struct hash_elem *_a, 
                          const struct hash_elem *_b, void *aux UNUSED) {
  struct page *a = hash_entry(_a, struct page, pages_elem);
  struct page *b = hash_entry(_b, struct page, pages_elem);
  return a->vaddr < b->vaddr;
}
