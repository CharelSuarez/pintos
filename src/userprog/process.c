#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include <hash.h>
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/syscall.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

static void process_file_destroy(struct hash_elem *e, void *aux UNUSED);

static void process_mmap_free(struct mmap_file* mmap_file);
static unsigned mmap_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool mmap_less_func(const struct hash_elem *_a, 
                           const struct hash_elem *_b, void *aux UNUSED);
static void process_mmap_destroy(struct hash_elem *e, void *aux UNUSED);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Store info about this process and pass it to start_process. */
  struct process_info *info = malloc(sizeof(struct process_info));
  if (!info) {
    return TID_ERROR;
  }
  sema_init(&info->alive_sema, 0);
  sema_init(&info->load_sema, 0);
  info->file_name = fn_copy;
  info->exit_status = 0;
  list_push_back(&thread_current()->children, &info->children_elem);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, info);
  
  if (tid != TID_ERROR) {
    /* Wait until start_process finishes. */
    sema_down(&info->load_sema);
    if (info->failed_loading) {
      tid = TID_ERROR;
    }
  }

  info->file_name = NULL;
  palloc_free_page (fn_copy);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *info_aux)
{
  struct intr_frame if_;
  bool success;

  // Store thread information for process_execute.
  struct thread *t = thread_current();
  struct process_info* info = (struct process_info*) info_aux;
  info->thread = t;
  info->tid = t->tid;
  char *file_name = info->file_name;

  // Initialize process-related fields for the thread.
  t->process_info = info;
  t->fd_counter = 2;
  t->this_exec = NULL;
  hash_init(&t->files, process_fd_hash_func, process_fd_less_func, NULL);
#ifdef VM
  page_init(t);
  hash_init(&t->mmap_files, mmap_hash_func, mmap_less_func, NULL);
#endif

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, notify process_execute and quit. */
  info->failed_loading = !success;
  sema_up(&info->load_sema);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting. */
int
process_wait (tid_t child_tid) 
{
  struct thread* cur = thread_current();
  for (struct list_elem* e = list_begin(&cur->children); 
       e != list_end(&cur->children); e = list_next(e)) {
    struct process_info* child = list_entry(e, struct process_info, 
                                             children_elem);
    if (child->tid != child_tid) {
      continue;
    }
    sema_down(&child->alive_sema);
    int exit_status = child->exit_status;
    list_remove(e);
    free(child);
    return exit_status;
  }
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* If parent is alive, indicate that child is dead. */
  struct process_info* info = cur->process_info;
  if (info != NULL) {
    /* Notify any waiting parent. */
    info->thread = NULL;
    sema_up(&info->alive_sema);
  }

  if (cur->this_exec != NULL) {
      file_allow_write(cur->this_exec);
      file_close(cur->this_exec);
      cur->this_exec = NULL;
  }

  /* Close all files opened by this process. */
  hash_destroy(&cur->files, process_file_destroy);
  hash_destroy(&cur->mmap_files, process_mmap_destroy);

  /* Free all children process_info. */
  struct list_elem* e = list_begin(&cur->children);
  while (e != list_end(&cur->children)) {
    struct process_info* child_info = list_entry(e, struct process_info, 
                                                 children_elem);
    e = list_remove(e);
    /* Indicate that the thread's parent has exited. */
    if (child_info->thread != NULL) {
      child_info->thread->process_info = NULL;
    }
    free(child_info);
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const char* command);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
static bool put_args(void **esp, const char* command);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Split the file name from the command. */
  char file_copy[NAME_MAX + 1];
  strlcpy(file_copy, file_name, NAME_MAX + 1);
  char *save_ptr;
  char *file_name_only = strtok_r(file_copy, " ", &save_ptr);

  /* Open executable file. */
  file = filesys_open (file_name_only);
  if (file == NULL) {
      printf ("load: %s: open failed\n", file_name_only);
      goto done; 
  }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name_only);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, file_name))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  
  /* If loading is successful, deny writing to executable. */
  t->this_exec = file;
  file_deny_write(file);

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      struct frame* frame = frame_allocate(); // TODO Lazy load the executable!
      if (frame == NULL)
        return false;
      uint8_t *kpage = frame->frame;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          frame_free(frame);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!page_create_with_frame(upage, frame, writable)) 
        {
          frame_free(frame);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const char* command) 
{
  void* kpage = page_create(((uint8_t *) PHYS_BASE) - PGSIZE, true, true);
  if (!kpage) {
    return false;
  }

  *esp = PHYS_BASE;
  put_args(esp, command);
  return true;
}

/* Parses the command into arguments and puts
   each argument on the stack for the main function. */
static bool 
put_args(void **esp, const char* command) {
  const int arg_limit = PGSIZE / 16;
  char* argv[arg_limit];
  int argc = 0;

  // Parse args and find arg count.
  char *save_ptr;
  char args_copy[PGSIZE / 8];
  strlcpy(args_copy, command, PGSIZE / 8);
  for (char *arg = strtok_r(args_copy, " ", &save_ptr);
       argc < arg_limit && arg != NULL;
       arg = strtok_r(NULL, " ", &save_ptr)) {
    argv[argc++] = arg;
  }

  // Copy the args' values, and word-align.
  void* argv_address = *esp;
  for (int i = argc - 1; i >= 0; i--) {
    *esp -= strlen(argv[i]) + 1;
    strlcpy(*esp, argv[i], strlen(argv[i]) + 1);
  }
  *esp -= 4 - ((size_t) *esp & 0x4);

  // Push the null-pointer sentinel and args' addresses.
  *esp -= sizeof(char*);
  *((char**) *esp) = NULL;
  for (int i = argc - 1; i >= 0; i--) {
    *esp -= sizeof(char**);
    argv_address -= strlen(argv[i]) + 1;
    *((char**) *esp) = argv_address;
  }

  // Push address of argv (one pointer upwards).
  *esp -= sizeof(char**);
  *((char***) *esp) = *esp + sizeof(char**);

  // Push argc.
  *esp -= sizeof(int);
  *((int*) *esp) = argc;

  // A return address of all time (look at it upside down).
  *esp -= sizeof(void*);
  *((int*) *esp) = 07734; // hi there

  return true;
}

unsigned 
process_fd_hash_func(const struct hash_elem *e, void *aux UNUSED) {
  struct process_file *file = hash_entry(e, struct process_file, files_elem);
  return hash_int(file->fd);
}

bool 
process_fd_less_func(const struct hash_elem *_a, 
                          const struct hash_elem *_b, void *aux UNUSED) {
  struct process_file *a = hash_entry(_a, struct process_file, files_elem);
  struct process_file *b = hash_entry(_b, struct process_file, files_elem);
  return a->fd < b->fd;
}

int 
process_open_file(const char* file_) {
  struct file* file = filesys_open(file_);
  if (!file) {
    return -1;
  }
  struct thread* curr = thread_current();
  struct process_file* process_file = malloc(sizeof(struct process_file));
  if (!process_file) {
    return -1;
  }
  process_file->file = file;
  process_file->fd = curr->fd_counter++;
  hash_insert(&curr->files, &process_file->files_elem);
  return process_file->fd;
}

struct file* 
process_get_file(int fd) {
  struct process_file process_file;
  process_file.fd = fd;
  struct hash_elem* file = hash_find(&thread_current()->files,
                                     &process_file.files_elem);
  if (!file) {
    return NULL;
  }
  return hash_entry(file, struct process_file, files_elem)->file;
}

void 
process_close_file(int fd) {
  struct process_file process_file;
  process_file.fd = fd;
  struct hash_elem* file_ = hash_delete(&thread_current()->files, 
                                        &process_file.files_elem);
  if (!file_) {
    return;
  }
  struct process_file* file = 
    hash_entry(file_, struct process_file, files_elem);
  file_close(file->file);
  free(file);
}

static void 
process_file_destroy(struct hash_elem *e, void *aux UNUSED) {
  struct process_file *file = hash_entry(e, struct process_file, files_elem);
  file_close(file->file);
  free(file);
}

mapid_t 
process_mmap_file(int fd, void* addr) {
  struct file* file = process_get_file(fd);
  if (!file) {
    return MAP_FAILED;
  }
  // Re-open the file in case the user closes it.
  file = file_reopen(file);
  if (!file) {
    return MAP_FAILED;
  }
  off_t length = file_length(file);
  if (length == 0) {
    return MAP_FAILED;
  }
  struct mmap_file* mmap_file = malloc(sizeof(struct mmap_file));
  if (!mmap_file) {
    return MAP_FAILED;
  }

  mmap_file->file = file;
  mmap_file->pages = calloc(sizeof(struct page*), 
                            (length + PGSIZE - 1) / PGSIZE);
  if (!mmap_file->pages) {
    free(mmap_file);
    return MAP_FAILED;
  }
  mmap_file->page_count = 0;
  for (off_t i = 0; i < length; i += PGSIZE) {
    size_t size = (i + PGSIZE) > length ? length - i : PGSIZE;
    struct page* page = page_create_mmap(addr + i, file, i, size);
    if (!page) {
      // Free pages and malloc'd memory.
      process_mmap_free(mmap_file);
      return MAP_FAILED;
    }
    mmap_file->pages[mmap_file->page_count++] = page;
  }

  struct thread* curr = thread_current();
  mmap_file->mapid = curr->fd_counter++;
  hash_insert(&curr->mmap_files, &mmap_file->mmaps_elem);
  return mmap_file->mapid;
}

void 
process_mmap_close_file(mapid_t mapid) {
  struct mmap_file mmap_file;
  mmap_file.mapid = mapid;
  struct hash_elem* file_ = hash_delete(&thread_current()->mmap_files,
                                        &mmap_file.mmaps_elem);
  if (!file_) {
    return;
  }
  struct mmap_file* file = hash_entry(file_, struct mmap_file, mmaps_elem);
  process_mmap_free(file);
}

/* Given a mmap_file, frees all allocated pages, the page list,
   and, the mmap_file itself. */
static void 
process_mmap_free(struct mmap_file* mmap_file) {
  for (size_t i = 0; i < mmap_file->page_count; i++) {
    page_free(mmap_file->pages[i]);
  }
  free(mmap_file->pages);
  file_close(mmap_file->file);
  free(mmap_file);
}

static unsigned 
mmap_hash_func(const struct hash_elem *e, void *aux UNUSED) {
  struct mmap_file *file = hash_entry(e, struct mmap_file, mmaps_elem);
  return hash_int(file->mapid);
}

static bool 
mmap_less_func(const struct hash_elem *_a, 
                          const struct hash_elem *_b, void *aux UNUSED) {
  struct mmap_file *a = hash_entry(_a, struct mmap_file, mmaps_elem);
  struct mmap_file *b = hash_entry(_b, struct mmap_file, mmaps_elem);
  return a->mapid < b->mapid;
}

static void 
process_mmap_destroy(struct hash_elem *e, void *aux UNUSED) {
  struct mmap_file *file = hash_entry(e, struct mmap_file, mmaps_elem);
  process_mmap_free(file);
}
