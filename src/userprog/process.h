#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/file.h"

struct child_process {
  tid_t tid;
  int exit_status;
  struct list_elem children_elem;
  struct semaphore alive_sema;
  struct semaphore load_sema;
  char* file_name;
};

struct process_file {
  int fd;
  struct file *file;
  struct hash_elem files_elem;
};

int process_open_file(const char* file);
struct file* process_get_file(int fd);
void process_close_file(int fd);
unsigned process_fd_hash_func(const struct hash_elem *e, void *aux UNUSED);
bool process_fd_less_func(const struct hash_elem *a, 
                          const struct hash_elem *b, void *aux UNUSED);

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
