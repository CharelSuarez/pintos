#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/file.h"
#ifdef VM
#include "userprog/syscall.h"
#endif

struct process_info {
  struct thread* thread;          /* The thread for this process. */
  tid_t tid;                      /* The pid/tid of this process. */
  int exit_status;                /* The exit status of this process. */
  struct semaphore alive_sema;    /* A semaphore held while this process 
                                      is running. */
  struct semaphore load_sema;     /* A semaphore held while this process is 
                                      loading its executable. */
  char* file_name;                /* The executable file name, may be null. */
  bool failed_loading;            /* If the executable successfully loaded. */

  /* Owned by threads/thread.c. */
  struct list_elem children_elem; /* An elem for thread.h's child list. */
};

struct process_file {
  int fd;                      /* The file descriptor for this file. */
  struct file* file;           /* The file. */

  /* Owned by threads/thread.c. */
  struct hash_elem files_elem; /* An elem for thread.h's file table. */
};

#ifdef VM
struct mmap_file {
  mapid_t mapid;               /* The map id for this mmap'd file. */
  struct page** pages;          /* The array of mmap'd pages. */
  size_t page_count;           /* Page count. */
  struct file* file;

  /* Owned by userprog/process.c. */
  struct hash_elem mmaps_elem; /* An elem for thread.h's mmap table. */
};
#endif

int process_open_file(const char* file);
struct file* process_get_file(int fd);
void process_close_file(int fd);
unsigned process_fd_hash_func(const struct hash_elem *e, void *aux UNUSED);
bool process_fd_less_func(const struct hash_elem *a, 
                          const struct hash_elem *b, void *aux UNUSED);

mapid_t process_mmap_file(int fd, void* addr);
void process_mmap_close_file(mapid_t mapid);

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
