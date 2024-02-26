#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/directory.h"
#include "devices/shutdown.h"
#include <string.h>
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "devices/input.h"

#define READ_ERROR 0xFFFFFFFF
#define EXIT_FAILURE -1

struct lock filesystem_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesystem_lock);
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, or exits the
   program if there was a segfault. */
static uint32_t
get_byte_or_die (const uint8_t* uaddr)
{
  if (!is_user_vaddr(uaddr)) {
    exit(EXIT_FAILURE);
  }
  uint32_t result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  if (result == READ_ERROR) {
    exit(EXIT_FAILURE);
  }
  return result;
}

/* Reads a dword (4 bytes) at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the dword value if successful, or exits the
   program if there was a segfault. */
static uint32_t
get_dword_or_die (uint8_t *uaddr)
{
  uint32_t value = 0;
  uint8_t* dest = (uint8_t*) &value;
  for (size_t size = sizeof(uint32_t); size > 0; size--, dest++, uaddr++) {
    *dest = get_byte_or_die(uaddr);
  }
  return value;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
// static bool
// put_user (uint8_t *udst, uint8_t byte)
// {
//   int error_code;
//   asm ("movl $1f, %0; movb %b2, %1; 1:"
//        : "=&a" (error_code), "=m" (*udst) : "q" (byte));
//   return error_code != -1;
// }

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t syscall = get_dword_or_die(f->esp);
  switch (syscall) {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT: {
      int status = get_dword_or_die(f->esp + 4);
      exit(status);
      break;
    }
    case SYS_EXEC: {
      const char* cmd_line = (const char*) get_dword_or_die(f->esp + 4);
      f->eax = exec(cmd_line);
      break;
    }
    case SYS_WAIT: {
      tid_t pid = get_dword_or_die(f->esp + 4);
      f->eax = wait(pid);
      break;
    }
    case SYS_CREATE: {
      const char* file = (const char*) get_dword_or_die(f->esp + 4);
      unsigned initial_size = (unsigned) get_dword_or_die(f->esp + 8);
      f->eax = create(file, initial_size);
      break;
    }
    case SYS_REMOVE: {
      const char* file = (const char*) get_dword_or_die(f->esp + 4);
      f->eax = remove(file);
      break;
    } 
    case SYS_OPEN: {
      const char* file = (const char*) get_dword_or_die(f->esp + 4);
      f->eax = open(file);
      break;
    }
    case SYS_FILESIZE: {
      int fd = get_dword_or_die(f->esp + 4);
      f->eax = filesize(fd);
      break;
    }
    case SYS_READ: {
      int fd = get_dword_or_die(f->esp + 4);
      void *buffer = (void*) get_dword_or_die(f->esp + 8);
      unsigned size = (unsigned) get_dword_or_die(f->esp + 12);
      f->eax = read(fd, buffer, size);
      break;
    }
    case SYS_WRITE: {
      int fd = get_dword_or_die(f->esp + 4);
      const void *buffer = (const void*) get_dword_or_die(f->esp + 8);
      unsigned size = (unsigned) get_dword_or_die(f->esp + 12);
      f->eax = write(fd, buffer, size);
      break;
    }
    case SYS_SEEK: {
      int fd = get_dword_or_die(f->esp + 4);
      unsigned position = get_dword_or_die(f->esp + 8);
      seek(fd, position);
      break;
    }
    case SYS_TELL: {
      int fd = get_dword_or_die(f->esp + 4);
      f->eax = tell(fd);
      break;
    }
    case SYS_CLOSE: {
      int fd = get_dword_or_die(f->esp + 4);
      close(fd);
      break;
    }
  }
}

/* Checks that the given string address is properly 
   accessable until its first null terminator, or 
   exits the program if there is a segfault. */
static void
check_string_or_die(const char* str) {
  char* address = (char*) str;
  while (get_byte_or_die((void*) address) != '\0') {
    address++;
  }
}

/* Checks that the given string address is properly 
   accessable until the byte at offset size, or 
   exits the program if there is a segfault. */
static void
check_buffer_or_die(const void* buffer, unsigned size) {
  void* address = (void*) buffer;
  unsigned read = 0;
  while (read < size) {
    get_byte_or_die(address);
    read++;
    address++;
  }
}

void
halt(void) {
  shutdown_power_off();
}

void
exit(int status) {  
  struct thread *t = thread_current();
  /* Split the file name from the command. */
  char file_copy[NAME_MAX + 1];
  strlcpy(file_copy, t->name, NAME_MAX + 1);
  char *save_ptr;
  char *file_name_only = strtok_r(file_copy, " ", &save_ptr);

  printf("%s: exit(%d)\n", file_name_only, status);
  /* Set exit status for waiting parent. */
  if (t->process_info != NULL) {
    t->process_info->exit_status = status;
  }
  thread_exit();
}

tid_t
exec(const char* cmd_line) {
  check_string_or_die(cmd_line);
  lock_acquire(&filesystem_lock);
  tid_t pid = process_execute(cmd_line);
  lock_release(&filesystem_lock);
  return pid;
}

int
wait(tid_t pid) {
  return process_wait(pid);
}

bool
create(const char *file, unsigned initial_size) {
  check_string_or_die(file);
  lock_acquire(&filesystem_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesystem_lock);
  return success;
}

bool
remove(const char *file) {
  check_string_or_die(file);
  lock_acquire(&filesystem_lock);
  bool success = filesys_remove(file);
  lock_release(&filesystem_lock);
  return success;
}

int
open(const char *file) {
  check_string_or_die(file);
  lock_acquire(&filesystem_lock);
  int fd = process_open_file(file);
  lock_release(&filesystem_lock);
  return fd;
}

int
filesize(int fd) {
  lock_acquire(&filesystem_lock);
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    lock_release(&filesystem_lock);
    return -1;
  }
  int size = file_length(file);
  lock_release(&filesystem_lock);
  return size;
}

int
read(int fd, void* buffer, unsigned size) {
  if (size <= 0) {
    return 0;
  }
  check_buffer_or_die(buffer, size);
  if (fd == STDIN_FILENO) {
    for (unsigned i = 0; i < size; i++) {
      ((char*) buffer)[i] = input_getc();
    }
    return (int) size;
  }
  lock_acquire(&filesystem_lock);
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    lock_release(&filesystem_lock);
    return -1;
  }
  int bytes = file_read(file, buffer, size);
  lock_release(&filesystem_lock);
  return bytes;
}

int
write (int fd, const void *buffer, unsigned size) {
  if (size <= 0) {
    return 0;
  }
  check_buffer_or_die(buffer, size);
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    return (int) size;
  }
  lock_acquire(&filesystem_lock);
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    lock_release(&filesystem_lock);
    return -1;
  }
  int bytes = file_write(file, buffer, size);
  lock_release(&filesystem_lock);
  return bytes;
}

void
seek(int fd, unsigned position) {
  lock_acquire(&filesystem_lock);
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    lock_release(&filesystem_lock);
    return;
  }
  file_seek(file, position);
  lock_release(&filesystem_lock);
}

unsigned
tell(int fd) {
  lock_acquire(&filesystem_lock);
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    lock_release(&filesystem_lock);
    return -1;
  }
  unsigned position = file_tell(file);
  lock_release(&filesystem_lock);
  return position;
}

void
close(int fd) {
  lock_acquire(&filesystem_lock);
  process_close_file(fd);
  lock_release(&filesystem_lock);
}