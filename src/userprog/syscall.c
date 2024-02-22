#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/directory.h"

static void syscall_handler (struct intr_frame *);
static unsigned write (int fd, const void *buffer, unsigned size);
static void exit (int status);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static void 
copy_in(void *dst_, const void *src_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *src = src_;

  for (; size > 0; size--, dst++, src++) 
    *dst = get_user (src);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t syscall = *(uint32_t *)(f->esp);
  switch (syscall) {
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_EXIT: {
      int status = *(int *)(f->esp + 4);
      exit(status);
      break;
    }
    // case SYS_EXEC:
    //   break;
    // case SYS_WAIT:
    //   break;
    // case SYS_CREATE:
    //   break;
    // case SYS_REMOVE:
    //   break;
    // case SYS_OPEN:
    //   break;
    // case SYS_FILESIZE:
    //   break;
    // case SYS_READ:
    //   break;
    case SYS_WRITE: {
      int fd = *(int *)(f->esp + 4);
      const void *buffer = *(const void **)(f->esp + 8);
      unsigned size = *(unsigned *)(f->esp + 12);
      f->eax = write(fd, buffer, size);
      break;
    }
    // case SYS_SEEK:
    //   break;
    // case SYS_TELL:
    //   break;
    // case SYS_CLOSE:
    //   break;
    default: {
      // printf ("system call!\n");
      // thread_exit ();
    }
  }
}

static unsigned
write (int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    return size;
  } else {
    return -1; // TODO Implement file stuff.
  }
}

static void
exit (int status)
{  
  /* Split the file name from the command. */
  char file_copy[NAME_MAX + 1];
  strlcpy(file_copy, thread_name(), NAME_MAX + 1);
  char *save_ptr;
  char *file_name_only = strtok_r(file_copy, " ", &save_ptr);

  printf("%s: exit(%d)\n", file_copy, status);
  thread_current()->exit_status = status;
  thread_exit();
}