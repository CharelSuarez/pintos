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
#ifdef VM
#include "vm/page.h"
#endif
#ifdef FILESYS
#include "filesys/directory.h"
#endif

#define READ_ERROR 0xFFFFFFFF
#define CODE_SEGMENT ((void*) 0x08048000)
#define SYSCALL_EXIT_FAILURE -1

struct lock* filesystem_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  filesystem_lock = process_get_filesys_lock();
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, or exits the
   program if there was a segfault. */
static uint32_t
get_byte_or_die (const uint8_t* uaddr)
{
  if (!is_user_vaddr(uaddr)) {
    exit(SYSCALL_EXIT_FAILURE);
  }
  uint32_t result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  if (result == READ_ERROR) {
    exit(SYSCALL_EXIT_FAILURE);
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

static void
syscall_handler (struct intr_frame *f) 
{
  thread_current()->saved_esp = f->esp;
  struct page* page = page_find(f->esp);
  if (!page) {
    exit(SYSCALL_EXIT_FAILURE);
  }
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
      pid_t pid = get_dword_or_die(f->esp + 4);
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
    case SYS_MMAP: {
      int fd = get_dword_or_die(f->esp + 4);
      void* addr = (void*) get_dword_or_die(f->esp + 8);
      f->eax = mmap(fd, addr);
      break;
    }
    case SYS_MUNMAP: {
      mapid_t mapid = get_dword_or_die(f->esp + 4);
      munmap(mapid);
      break;
    }
    case SYS_CHDIR: {
      const char* dir = (const char*) get_dword_or_die(f->esp + 4);
      f->eax = chdir(dir);
      break;
    }
    case SYS_MKDIR: {
      const char* dir = (const char*) get_dword_or_die(f->esp + 4);
      f->eax = mkdir(dir);
      break;
    }
    case SYS_READDIR: {
      int fd = get_dword_or_die(f->esp + 4);
      char* name = (char*) get_dword_or_die(f->esp + 8);
      f->eax = readdir(fd, name);
      break;
    }
    case SYS_ISDIR: {
      int fd = get_dword_or_die(f->esp + 4);
      f->eax = isdir(fd);
      break;
    }
    case SYS_INUMBER: {
      int fd = get_dword_or_die(f->esp + 4);
      f->eax = inumber(fd);
      break;
    }
    default: {
      exit(SYSCALL_EXIT_FAILURE);
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

pid_t
exec(const char* cmd_line) {
  check_string_or_die(cmd_line);
  // Process functions are already synchronized.
  pid_t pid = process_execute(cmd_line);
  return pid;
}

int
wait(pid_t pid) {
  return process_wait(pid);
}

bool
create(const char *file, unsigned initial_size) {
  check_string_or_die(file);
  lock_acquire(filesystem_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(filesystem_lock);
  return success;
}

bool
remove(const char *file) {
  check_string_or_die(file);
  lock_acquire(filesystem_lock);
  bool success = filesys_remove(file);
  lock_release(filesystem_lock);
  return success;
}

int
open(const char *file) {
  check_string_or_die(file);
  // Process functions are already synchronized.
  int fd = process_open_file(file);
  return fd;
}

int
filesize(int fd) {
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL || file_is_dir(file)) {
    return -1;
  }
  lock_acquire(filesystem_lock);
  int size = file_length(file);
  lock_release(filesystem_lock);
  return size;
}

int
read(int fd, void* buffer, unsigned size) {
  check_buffer_or_die(buffer, size);
  if (fd == STDIN_FILENO) {
    lock_acquire(filesystem_lock);
    for (unsigned i = 0; i < size; i++) {
      ((char*) buffer)[i] = input_getc();
    }
    lock_release(filesystem_lock);
    return (int) size;
  }
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL || file_is_dir(file)) {
    return -1;
  }
  lock_acquire(filesystem_lock);
  int bytes = file_read(file, buffer, size);
  lock_release(filesystem_lock);
  return bytes;
}

int
write (int fd, const void *buffer, unsigned size) {
  check_buffer_or_die(buffer, size);
  if (fd == STDOUT_FILENO) {
    lock_acquire(filesystem_lock);
    putbuf(buffer, size);
    lock_release(filesystem_lock);
    return (int) size;
  }
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL || file_is_dir(file)) {
    return -1;
  }
  lock_acquire(filesystem_lock);
  int bytes = file_write(file, buffer, size);
  lock_release(filesystem_lock);
  return bytes;
}

void
seek(int fd, unsigned position) {
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL || file_is_dir(file)) {
    return;
  }
  lock_acquire(filesystem_lock);
  file_seek(file, position);
  lock_release(filesystem_lock);
}

unsigned
tell(int fd) {
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL || file_is_dir(file)) {
    return -1;
  }
  lock_acquire(filesystem_lock);
  unsigned position = file_tell(file);
  lock_release(filesystem_lock);
  return position;
}

void
close(int fd) {
  // Process functions are already synchronized.
  process_close_file(fd);
}

mapid_t
mmap(int fd, void *addr) {
#ifdef VM
  if (addr < CODE_SEGMENT || !is_user_vaddr(addr) || 
      pg_round_down(addr) != addr || fd == STDIN_FILENO 
      || fd == STDOUT_FILENO) {
    return MAP_FAILED;
  }
  // Process functions are already synchronized.
  mapid_t mapid = process_mmap_file(fd, addr);
  return mapid;
#else
  return MAP_FAILED;
#endif
}

void
munmap(mapid_t mapid) {
#ifdef VM
  // Process functions are already synchronized.
  process_mmap_close_file(mapid);
#else
  return MAP_FAILED;
#endif
}

bool
chdir(const char *dir) {
  check_string_or_die(dir);
  lock_acquire(filesystem_lock);
  struct file* new_dir = filesys_open_dir(dir);
  if (new_dir == NULL || !file_is_dir(new_dir)) {
    lock_release(filesystem_lock);
    file_close(new_dir);
    return false;
  }
  struct thread* t = thread_current();
  file_close(t->working_dir);
  t->working_dir = new_dir;
  lock_release(filesystem_lock);
  return true;
}

bool
mkdir(const char *dir) {
  check_string_or_die(dir);
  lock_acquire(filesystem_lock);
  bool success = filesys_create_dir(dir, 0);
  lock_release(filesystem_lock);
  return success;
}

bool
readdir(int fd, char *name) {
  check_buffer_or_die(name, READDIR_MAX_LEN + 1);
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL || !file_is_dir(file)) {
    return false;
  }
  lock_acquire(filesystem_lock);
  bool success = dir_readdir(file, name);
  lock_release(filesystem_lock);
  return success;
}

bool
isdir(int fd) {
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    return false;
  }
  return file_is_dir(file);
}

int
inumber(int fd) {
  // Process functions are already synchronized.
  struct file* file = process_get_file(fd);
  if (file == NULL) {
    return -1;
  }
  return (int) file_get_inumber(file);
}
